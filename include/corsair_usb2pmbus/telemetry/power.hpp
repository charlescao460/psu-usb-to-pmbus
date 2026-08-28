#pragma once

#include "corsair_usb2pmbus/telemetry/snapshot.hpp"

namespace cusb2pmbus
{

// iCUE presents aggregate rail power as VOUT * IOUT rather than exposing the
// AX1600i's native POUT register verbatim. Keep this calculation in one place
// so every published snapshot uses the same rule.
void update_derived_output_power(TelemetrySnapshot& snapshot) noexcept;

// AC/DC Server Profile READ_EIN is not a watt-hour counter. It is a Direct-
// format sum of integer-watt input-power samples plus aligned rollover and
// sample counters. Add one completed native READ_PIN sample to that state.
void accumulate_input_power_sample(TelemetrySnapshot& snapshot, float watts) noexcept;

} // namespace cusb2pmbus
