from __future__ import annotations

from collections.abc import Iterable


def update(crc: int, value: int) -> int:
    crc ^= value
    for _ in range(8):
        crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def calculate(data: bytes | bytearray | Iterable[int], initial: int = 0) -> int:
    crc = initial
    for value in data:
        crc = update(crc, value)
    return crc

