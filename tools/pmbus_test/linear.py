from __future__ import annotations

import math


def _signed(value: int, bits: int) -> int:
    sign = 1 << (bits - 1)
    return (value ^ sign) - sign


def decode_linear11(raw: int) -> float:
    exponent = _signed((raw >> 11) & 0x1F, 5)
    mantissa = _signed(raw & 0x7FF, 11)
    return math.ldexp(float(mantissa), exponent)


def encode_linear11(value: float) -> int:
    if not math.isfinite(value) or value == 0:
        return 0
    exponent = -16
    scaled = math.ldexp(value, -exponent)
    while not -1024 <= scaled <= 1023 and exponent < 15:
        exponent += 1
        scaled = math.ldexp(value, -exponent)
    mantissa = max(-1024, min(1023, round(scaled)))
    return ((exponent & 0x1F) << 11) | (mantissa & 0x7FF)


def decode_linear16(raw: int, exponent: int) -> float:
    return math.ldexp(float(raw), exponent)

