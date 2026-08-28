#include "psu_usb_to_pmbus/telemetry/power.hpp"

#include <algorithm>
#include <cmath>

namespace
{
constexpr std::uint32_t kReadEinAccumulatorRollover = 0x7FFFU;
constexpr std::uint32_t kReadEinSampleCountMask = 0xFFFFFFU;
} // namespace

namespace psu_usb_to_pmbus
{

void update_derived_output_power(TelemetrySnapshot& snapshot) noexcept
{
   float total = 0.0F;
   for (auto& rail : snapshot.rails)
   {
      if (!rail.valid)
      {
         continue;
      }
      rail.power = rail.voltage * rail.current;
      total += rail.power;
   }
   snapshot.total_output_power = total;
}

void accumulate_input_power_sample(TelemetrySnapshot& snapshot, float watts) noexcept
{
   if (!std::isfinite(watts) || watts < 0.0F)
   {
      return;
   }

   const auto sample = static_cast<std::uint32_t>(
      std::clamp(watts + 0.5F, 0.0F, static_cast<float>(kReadEinAccumulatorRollover)));
   auto accumulator = static_cast<std::uint32_t>(snapshot.input_power_accumulator) + sample;
   if (accumulator > kReadEinAccumulatorRollover)
   {
      accumulator -= kReadEinAccumulatorRollover;
      ++snapshot.input_power_rollover_count;
   }

   snapshot.input_power_accumulator = static_cast<std::uint16_t>(accumulator);
   snapshot.input_power_sample_count =
      (snapshot.input_power_sample_count + 1U) & kReadEinSampleCountMask;
}

} // namespace psu_usb_to_pmbus
