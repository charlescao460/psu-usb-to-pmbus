from __future__ import annotations

from dataclasses import dataclass
from typing import Protocol


class SerialLike(Protocol):
    def write(self, data: bytes) -> int: ...
    def readline(self) -> bytes: ...
    def close(self) -> None: ...


class BridgeError(RuntimeError):
    def __init__(self, code: str, detail: str, sequence: int | None = None) -> None:
        super().__init__(f"{code}: {detail}")
        self.code = code
        self.detail = detail
        self.sequence = sequence


@dataclass(frozen=True)
class BridgeInfo:
    version: str
    max_write: int
    max_read: int
    repeated_start: bool
    i2c_hz: int


class Bridge:
    def __init__(self, port: str | None = None, baudrate: int = 115200,
                 timeout: float = 1.0, serial_instance: SerialLike | None = None) -> None:
        if serial_instance is None:
            if port is None:
                raise ValueError("port is required")
            import serial
            serial_instance = serial.Serial(port, baudrate, timeout=timeout)
        self._serial = serial_instance
        self._sequence = 0

    def close(self) -> None:
        self._serial.close()

    def __enter__(self) -> "Bridge":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def _request(self, command: str) -> list[str]:
        self._sequence += 1
        sequence = self._sequence
        line = f"{command[0]} {sequence}{command[1:]}\n".encode("ascii")
        if self._serial.write(line) != len(line):
            raise BridgeError("SERIAL", "short write", sequence)
        raw = self._serial.readline()
        if not raw:
            raise BridgeError("TIMEOUT", "no bridge response", sequence)
        try:
            fields = raw.decode("ascii").strip().split()
        except UnicodeDecodeError as exc:
            raise BridgeError("SERIAL", "non-ASCII response", sequence) from exc
        if len(fields) < 2:
            raise BridgeError("SERIAL", f"malformed response {raw!r}", sequence)
        try:
            response_sequence = int(fields[1], 10)
        except ValueError as exc:
            raise BridgeError("SERIAL", "invalid response sequence", sequence) from exc
        if response_sequence != sequence:
            raise BridgeError("SEQUENCE", f"expected {sequence}, got {response_sequence}", sequence)
        if fields[0] == "ERR":
            code = fields[2] if len(fields) > 2 else "UNKNOWN"
            detail = " ".join(fields[3:]) if len(fields) > 3 else ""
            raise BridgeError(code, detail, sequence)
        if fields[0] != "OK":
            raise BridgeError("SERIAL", f"unexpected status {fields[0]}", sequence)
        return fields[2:]

    def info(self) -> BridgeInfo:
        fields = self._request("I")
        if not fields:
            raise BridgeError("SERIAL", "missing bridge information")
        values: dict[str, str] = {}
        for field in fields[1:]:
            if "=" in field:
                key, value = field.split("=", 1)
                values[key] = value
        return BridgeInfo(
            version=fields[0],
            max_write=int(values.get("MAXW", "0")),
            max_read=int(values.get("MAXR", "0")),
            repeated_start=values.get("RS") == "1",
            i2c_hz=int(values.get("I2C_HZ", "0")),
        )

    def transact(self, address: int, write: bytes = b"", read_length: int = 0,
                 repeated_start: bool = True) -> bytes:
        if not 0 <= address <= 0x7F:
            raise ValueError("address must be a 7-bit value")
        if not 0 <= read_length <= 32 or len(write) > 32:
            raise ValueError("transaction exceeds bridge buffer")
        mode = "RS" if repeated_start else "STOP"
        write_hex = write.hex().upper() if write else "-"
        fields = self._request(f"X {address:#04x} {mode} {read_length} {write_hex}")
        if len(fields) != 1:
            raise BridgeError("SERIAL", "missing transaction payload")
        if fields[0] == "-":
            return b""
        try:
            result = bytes.fromhex(fields[0])
        except ValueError as exc:
            raise BridgeError("SERIAL", "invalid response hex") from exc
        if len(result) != read_length:
            raise BridgeError("SHORT_READ", f"expected {read_length}, got {len(result)}")
        return result

