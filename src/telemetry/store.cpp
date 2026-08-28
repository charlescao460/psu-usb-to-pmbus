#include "corsair_usb2pmbus/telemetry/store.hpp"

namespace cusb2pmbus
{

void TelemetryStore::publish(const TelemetrySnapshot& snapshot)
{
   const auto next = static_cast<std::uint8_t>(active_.load(std::memory_order_relaxed) ^ 1U);
   while (readers_[next].load(std::memory_order_acquire) != 0U)
   {
   }
   slots_[next] = snapshot;
   active_.store(next, std::memory_order_release);
}

TelemetrySnapshot TelemetryStore::read() const
{
   TelemetrySnapshot result{};
   for (;;)
   {
      const auto index = active_.load(std::memory_order_acquire);
      readers_[index].fetch_add(1, std::memory_order_acq_rel);
      if (index != active_.load(std::memory_order_acquire))
      {
         readers_[index].fetch_sub(1, std::memory_order_release);
         continue;
      }
      result = slots_[index];
      readers_[index].fetch_sub(1, std::memory_order_release);
      return result;
   }
}

} // namespace cusb2pmbus
