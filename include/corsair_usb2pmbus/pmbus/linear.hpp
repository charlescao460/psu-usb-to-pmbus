#pragma once

#include <cstdint>

namespace cusb2pmbus::pmbus
{

float decode_linear11(std::uint16_t raw);
std::uint16_t encode_linear11(float value);
float decode_linear16(std::uint16_t raw, std::int8_t exponent);
std::uint16_t encode_linear16(float value, std::int8_t exponent);

} // namespace cusb2pmbus::pmbus
