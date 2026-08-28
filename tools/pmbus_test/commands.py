from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
from math import isclose

from .bridge import Bridge
from .linear import decode_linear11, decode_linear16
from .pec import calculate


class Command(IntEnum):
    PAGE = 0x00
    CLEAR_FAULTS = 0x03
    PAGE_PLUS_WRITE = 0x05
    PAGE_PLUS_READ = 0x06
    CAPABILITY = 0x19
    QUERY = 0x1A
    SMBALERT_MASK = 0x1B
    VOUT_MODE = 0x20
    COEFFICIENTS = 0x30
    FAN_CONFIG_1_2 = 0x3A
    FAN_COMMAND_1 = 0x3B
    STATUS_BYTE = 0x78
    STATUS_WORD = 0x79
    STATUS_IOUT = 0x7B
    STATUS_INPUT = 0x7C
    STATUS_TEMPERATURE = 0x7D
    STATUS_CML = 0x7E
    STATUS_FANS_1_2 = 0x81
    READ_EIN = 0x86
    READ_VIN = 0x88
    READ_IIN = 0x89
    READ_VOUT = 0x8B
    READ_IOUT = 0x8C
    READ_TEMPERATURE_1 = 0x8D
    READ_TEMPERATURE_2 = 0x8E
    READ_FAN_SPEED_1 = 0x90
    READ_POUT = 0x96
    READ_PIN = 0x97
    PMBUS_REVISION = 0x98
    MFR_ID = 0x99
    MFR_MODEL = 0x9A
    MFR_REVISION = 0x9B
    MFR_SERIAL = 0x9E
    APP_PROFILE_SUPPORT = 0x9F
    MFR_IOUT_MAX = 0xA6
    MFR_POUT_MAX = 0xA7
    MFR_MAX_TEMP_1 = 0xC0
    MFR_MAX_TEMP_2 = 0xC1
    MFR_BRIDGE_STATUS = 0xD0
    MFR_UPTIME = 0xD2
    MFR_BRANCH_OCP = 0xEA


@dataclass(frozen=True)
class Reading:
    command: Command
    raw: int | bytes
    value: float | int | str
    unit: str = ""


@dataclass(frozen=True)
class InputEnergyAccumulator:
    """AC/DC Server Profile READ_EIN power-sample accumulator."""

    power_accumulator: int
    rollover_count: int
    sample_count: int

    @property
    def average_watts(self) -> float | None:
        if self.sample_count == 0:
            return None
        return ((self.rollover_count * 0x7FFF) + self.power_accumulator) / self.sample_count


def decode_read_ein(payload: bytes) -> InputEnergyAccumulator:
    if len(payload) != 6:
        raise ValueError(f"READ_EIN payload must be 6 bytes, got {len(payload)}")
    return InputEnergyAccumulator(
        power_accumulator=int.from_bytes(payload[0:2], "little"),
        rollover_count=payload[2],
        sample_count=int.from_bytes(payload[3:6], "little"),
    )


def output_power_matches_product(readings: list[Reading], *, rel_tol: float = 0.01,
                                 abs_tol: float = 0.5) -> bool:
    """Check the iCUE aggregate-page rule: READ_POUT = READ_VOUT * READ_IOUT."""
    by_command = {reading.command: reading for reading in readings}
    try:
        voltage = float(by_command[Command.READ_VOUT].value)
        current = float(by_command[Command.READ_IOUT].value)
        power = float(by_command[Command.READ_POUT].value)
    except (KeyError, TypeError, ValueError):
        return False
    return isclose(power, voltage * current, rel_tol=rel_tol, abs_tol=abs_tol)


class PecError(RuntimeError):
    pass


class PmbusClient:
    def __init__(self, bridge: Bridge, address: int = 0x58) -> None:
        if not 0 <= address <= 0x7F:
            raise ValueError("address must be a 7-bit value")
        self.bridge = bridge
        self.address = address

    @property
    def _write_address(self) -> int:
        return self.address << 1

    @property
    def _read_address(self) -> int:
        return (self.address << 1) | 1

    def read_data(self, command: int | Command, length: int,
                  request: bytes = b"") -> bytes:
        command_value = int(command)
        write = bytes((command_value,)) + request
        response = self.bridge.transact(self.address, write, length + 1, repeated_start=True)
        data, received_pec = response[:-1], response[-1]
        expected = calculate(bytes((self._write_address,)) + write +
                             bytes((self._read_address,)) + data)
        if received_pec != expected:
            raise PecError(f"PEC mismatch: expected 0x{expected:02X}, got 0x{received_pec:02X}")
        return data

    def read_byte(self, command: int | Command, request: bytes = b"") -> int:
        return self.read_data(command, 1, request)[0]

    def read_word(self, command: int | Command, request: bytes = b"") -> int:
        return int.from_bytes(self.read_data(command, 2, request), "little")

    def read_block(self, command: int | Command, request: bytes = b"",
                   maximum: int = 30) -> bytes:
        # Request maximum+count+PEC. Extra bytes after count+payload+PEC are ignored;
        # the target keeps them deterministic for fixed-length Wire transactions.
        write = bytes((int(command),)) + request
        response = self.bridge.transact(self.address, write, maximum + 2, repeated_start=True)
        count = response[0]
        if count > maximum:
            raise ValueError(f"block count {count} exceeds requested maximum {maximum}")
        data = response[1:1 + count]
        received_pec = response[1 + count]
        covered = bytes((self._write_address,)) + write + bytes((self._read_address, count)) + data
        expected = calculate(covered)
        if received_pec != expected:
            raise PecError(f"block PEC mismatch: expected 0x{expected:02X}, got 0x{received_pec:02X}")
        return data

    def write_data(self, command: int | Command, payload: bytes = b"") -> None:
        body = bytes((int(command),)) + payload
        outgoing = body + bytes((calculate(bytes((self._write_address,)) + body),))
        self.bridge.transact(self.address, outgoing, 0)

    def set_page(self, page: int) -> None:
        self.write_data(Command.PAGE, bytes((page,)))

    def query(self, command: int | Command) -> int:
        response = self.read_block(Command.QUERY, bytes((1, int(command))), maximum=1)
        return response[0]

    def coefficients(self, command: int | Command, *, for_read: bool = True) -> bytes:
        return self.read_block(Command.COEFFICIENTS,
                               bytes((2, int(command), int(for_read))), maximum=5)

    def page_plus_read(self, page: int, command: int | Command, length: int) -> bytes:
        response = self.read_data(Command.PAGE_PLUS_READ, length + 1,
                                  bytes((2, page, int(command))))
        if response[0] != length:
            raise ValueError(f"PAGE_PLUS_READ count {response[0]} != {length}")
        return response[1:]

    def page_plus_write(self, page: int, command: int | Command, payload: bytes) -> None:
        inner = bytes((page, int(command))) + payload
        if len(inner) > 0xFF:
            raise ValueError("PAGE_PLUS_WRITE payload is too large")
        self.write_data(Command.PAGE_PLUS_WRITE, bytes((len(inner),)) + inner)

    def write_alert_mask(self, status_command: int | Command, mask: int) -> None:
        if not 0 <= mask <= 0xFF:
            raise ValueError("alert mask must be one byte")
        self.write_data(Command.SMBALERT_MASK, bytes((int(status_command), mask)))

    def read_alert_mask(self, status_command: int | Command) -> int:
        response = self.read_block(Command.SMBALERT_MASK,
                                   bytes((1, int(status_command))), maximum=1)
        return response[0]

    def mz73_compatibility_writes(self, mask: int = 0xFF) -> None:
        """Replay the harmless writes observed in a live MZ73 polling cycle."""
        self.write_alert_mask(Command.STATUS_FANS_1_2, mask)
        self.page_plus_write(1, Command.SMBALERT_MASK,
                             bytes((Command.STATUS_INPUT, mask)))
        self.write_data(Command.MFR_BRIDGE_STATUS, b"\x00")

    def read_linear11(self, command: Command, unit: str = "") -> Reading:
        raw = self.read_word(command)
        return Reading(command, raw, decode_linear11(raw), unit)

    def read_vout(self) -> Reading:
        mode = self.read_byte(Command.VOUT_MODE)
        exponent = (mode & 0x1F) - 32 if mode & 0x10 else mode & 0x1F
        raw = self.read_word(Command.READ_VOUT)
        return Reading(Command.READ_VOUT, raw, decode_linear16(raw, exponent), "V")

    def read_text(self, command: Command) -> Reading:
        raw = self.read_block(command)
        return Reading(command, raw, raw.decode("ascii", errors="replace").rstrip("\0"))

    def read_ein(self) -> InputEnergyAccumulator:
        return decode_read_ein(self.read_block(Command.READ_EIN, maximum=6))

    def telemetry(self, page: int = 0) -> list[Reading]:
        self.set_page(page)
        readings = [
            self.read_linear11(Command.READ_VIN, "V"),
            self.read_linear11(Command.READ_IIN, "A"),
            self.read_linear11(Command.READ_PIN, "W"),
            self.read_vout(),
            self.read_linear11(Command.READ_IOUT, "A"),
            self.read_linear11(Command.READ_POUT, "W"),
            self.read_linear11(Command.READ_TEMPERATURE_1, "C"),
            self.read_linear11(Command.READ_TEMPERATURE_2, "C"),
            self.read_linear11(Command.READ_FAN_SPEED_1, "rpm"),
        ]
        return readings
