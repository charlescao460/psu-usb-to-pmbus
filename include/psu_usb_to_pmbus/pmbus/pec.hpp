#pragma once

#include <cstdint>
#include <span>

namespace psu_usb_to_pmbus::pmbus
{

std::uint8_t pec_update(std::uint8_t crc, std::uint8_t value);
std::uint8_t pec(std::span<const std::uint8_t> bytes, std::uint8_t initial = 0);

} // namespace psu_usb_to_pmbus::pmbus
