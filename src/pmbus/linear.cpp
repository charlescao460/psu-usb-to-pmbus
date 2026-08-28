#include "corsair_usb2pmbus/pmbus/linear.hpp"

#include <algorithm>
#include <cmath>

namespace cusb2pmbus::pmbus
{
namespace
{
std::int16_t sign_extend(std::uint16_t value, unsigned bits)
{
   const auto mask = static_cast<std::uint16_t>(1U << (bits - 1U));
   return static_cast<std::int16_t>((value ^ mask) - mask);
}
} // namespace

float decode_linear11(std::uint16_t raw)
{
   const auto exponent = sign_extend(static_cast<std::uint16_t>(raw >> 11U), 5);
   const auto mantissa = sign_extend(static_cast<std::uint16_t>(raw & 0x07FFU), 11);
   return std::ldexp(static_cast<float>(mantissa), exponent);
}

std::uint16_t encode_linear11(float value)
{
   if (!std::isfinite(value) || value == 0.0F)
   {
      return 0;
   }
   int exponent = -16;
   float scaled = std::ldexp(value, -exponent);
   while ((scaled > 1023.0F || scaled < -1024.0F) && exponent < 15)
   {
      ++exponent;
      scaled = std::ldexp(value, -exponent);
   }
   const auto mantissa = std::clamp(static_cast<int>(std::lround(scaled)), -1024, 1023);
   return static_cast<std::uint16_t>(((exponent & 0x1F) << 11) | (mantissa & 0x07FF));
}

float decode_linear16(std::uint16_t raw, std::int8_t exponent)
{
   return std::ldexp(static_cast<float>(raw), exponent);
}

std::uint16_t encode_linear16(float value, std::int8_t exponent)
{
   if (!std::isfinite(value) || value <= 0.0F)
   {
      return 0;
   }
   const auto scaled = std::ldexp(value, -exponent);
   return static_cast<std::uint16_t>(std::clamp(std::lround(scaled), 0L, 65535L));
}

} // namespace cusb2pmbus::pmbus
