#pragma once

#include <cstdint>

namespace psu_usb_to_pmbus
{

class PsuBackend
{
public:
   virtual ~PsuBackend() = default;
   virtual void task(std::uint64_t now_ms) = 0;
   virtual bool ready() const = 0;
};

} // namespace psu_usb_to_pmbus
