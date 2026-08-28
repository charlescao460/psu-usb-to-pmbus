from __future__ import annotations

from collections import deque

import pytest

from tools.pmbus_test.bridge import Bridge, BridgeError
from tools.pmbus_test.commands import (
    Command,
    PmbusClient,
    PecError,
    Reading,
    decode_read_ein,
    output_power_matches_product,
)
from tools.pmbus_test.linear import decode_linear11, encode_linear11
from tools.pmbus_test.pec import calculate


class FakeSerial:
    def __init__(self, responses: list[bytes]) -> None:
        self.responses = deque(responses)
        self.writes: list[bytes] = []
        self.closed = False

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data)

    def readline(self) -> bytes:
        return self.responses.popleft() if self.responses else b""

    def close(self) -> None:
        self.closed = True


def test_bridge_information_and_framing() -> None:
    serial = FakeSerial([
        b"OK 1 PMBusTestHost/1 MAXW=32 MAXR=32 RS=1 I2C_HZ=100000\n",
        b"OK 2 1234\n",
    ])
    bridge = Bridge(serial_instance=serial)
    info = bridge.info()
    assert info.max_read == 32
    assert info.repeated_start
    assert bridge.transact(0x58, b"\x88", 2) == b"\x12\x34"
    assert serial.writes == [b"I 1\n", b"X 2 0x58 RS 2 88\n"]


def test_bridge_structured_error() -> None:
    bridge = Bridge(serial_instance=FakeSerial([b"ERR 1 NACK_ADDR endTransmission\n"]))
    with pytest.raises(BridgeError, match="NACK_ADDR"):
        bridge.transact(0x58, b"\x19", 2)


def test_linear11_round_trip() -> None:
    for value in (0.0, 0.5, 3.3, 12.0, 230.0, 1600.0, -2.0):
        assert decode_linear11(encode_linear11(value)) == pytest.approx(value, rel=0.01, abs=0.02)


def test_smbus_crc_known_vector() -> None:
    assert calculate(b"123456789") == 0xF4


class FakeBridge:
    def __init__(self, data: bytes, corrupt: bool = False) -> None:
        self.data = data
        self.corrupt = corrupt
        self.writes: list[bytes] = []

    def transact(self, address: int, write: bytes = b"", read_length: int = 0,
                 repeated_start: bool = True) -> bytes:
        self.writes.append(write)
        covered = bytes((address << 1,)) + write + bytes(((address << 1) | 1,)) + self.data
        pec = calculate(covered) ^ int(self.corrupt)
        return self.data + bytes((pec,))


def test_client_word_and_pec() -> None:
    raw = encode_linear11(120.0)
    client = PmbusClient(FakeBridge(raw.to_bytes(2, "little")))  # type: ignore[arg-type]
    assert client.read_linear11(Command.READ_VIN).value == pytest.approx(120.0)
    bad = PmbusClient(FakeBridge(raw.to_bytes(2, "little"), corrupt=True))  # type: ignore[arg-type]
    with pytest.raises(PecError):
        bad.read_word(Command.READ_VIN)


def test_query_request_is_included_in_pec() -> None:
    bridge = FakeBridge(b"\x01\xA0")
    client = PmbusClient(bridge)  # type: ignore[arg-type]
    assert client.query(Command.READ_VIN) == 0xA0
    assert bridge.writes == [b"\x1A\x01\x88"]


def test_coefficients_uses_counted_process_call() -> None:
    bridge = FakeBridge(b"\x05\x01\x00\x00\x00\x00")
    client = PmbusClient(bridge)  # type: ignore[arg-type]
    assert client.coefficients(Command.READ_EIN) == b"\x01\x00\x00\x00\x00"
    assert bridge.writes == [b"\x30\x02\x86\x01"]


def test_mz73_reference_writes_match_captured_frames() -> None:
    bridge = FakeBridge(b"")
    client = PmbusClient(bridge)  # type: ignore[arg-type]
    client.mz73_compatibility_writes()
    assert bridge.writes == [
        b"\x1B\x81\xFF\x86",
        b"\x05\x04\x01\x1B\x7C\xFF\x0F",
        b"\xD0\x00\x50",
    ]


def test_alert_mask_process_call_is_counted() -> None:
    bridge = FakeBridge(b"\x01\xFF")
    client = PmbusClient(bridge)  # type: ignore[arg-type]
    assert client.read_alert_mask(Command.STATUS_INPUT) == 0xFF
    assert bridge.writes == [b"\x1B\x01\x7C"]


def test_icue_output_power_rule() -> None:
    readings = [
        Reading(Command.READ_VOUT, 0, 3.296875, "V"),
        Reading(Command.READ_IOUT, 0, 3.0, "A"),
        Reading(Command.READ_POUT, 0, 9.890625, "W"),
    ]
    assert output_power_matches_product(readings)

    readings[-1] = Reading(Command.READ_POUT, 0, 0.0, "W")
    assert not output_power_matches_product(readings)


def test_read_ein_profile_payload() -> None:
    value = decode_read_ein(b"\xD2\x04\x05\x03\x02\x01")
    assert value.power_accumulator == 1234
    assert value.rollover_count == 5
    assert value.sample_count == 0x010203
    assert value.average_watts == pytest.approx((5 * 0x7FFF + 1234) / 0x010203)
    with pytest.raises(ValueError, match="6 bytes"):
        decode_read_ein(b"\x00" * 5)
