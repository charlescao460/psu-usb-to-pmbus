#pragma once

#include "corsair_usb2pmbus/telemetry/snapshot.hpp"

#include <array>
#include <atomic>
#include <cstdint>

namespace cusb2pmbus
{

class TelemetryStore
{
public:
   void publish(const TelemetrySnapshot& snapshot);
   TelemetrySnapshot read() const;

private:
   std::array<TelemetrySnapshot, 2> slots_{};
   mutable std::array<std::atomic<std::uint8_t>, 2> readers_{};
   std::atomic<std::uint8_t> active_{0};
};

} // namespace cusb2pmbus
