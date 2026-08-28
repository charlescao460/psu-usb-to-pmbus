#include "psu_usb_to_pmbus/pmbus/pec.hpp"

namespace psu_usb_to_pmbus::pmbus
{

std::uint8_t pec_update(std::uint8_t crc, std::uint8_t value)
{
   crc ^= value;
   for (unsigned bit = 0; bit < 8; ++bit)
   {
      crc = static_cast<std::uint8_t>((crc & 0x80U) != 0U ? (crc << 1U) ^ 0x07U : crc << 1U);
   }
   return crc;
}

std::uint8_t pec(std::span<const std::uint8_t> bytes, std::uint8_t initial)
{
   for (const auto value : bytes)
   {
      initial = pec_update(initial, value);
   }
   return initial;
}

} // namespace psu_usb_to_pmbus::pmbus
