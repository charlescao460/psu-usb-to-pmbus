#pragma once

#include <cstdint>

namespace cusb2pmbus
{

class PsuBackend
{
public:
   virtual ~PsuBackend() = default;
   virtual void task(std::uint64_t now_ms) = 0;
   virtual bool ready() const = 0;
};

} // namespace cusb2pmbus
