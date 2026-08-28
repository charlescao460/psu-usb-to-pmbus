#pragma once

#include <array>
#include <cstdint>

namespace cusb2pmbus
{

struct RailTelemetry
{
   float voltage{};
   float current{};
   float power{};
   float ocp_limit{};
   bool valid{};
   float native_power{};
};

struct RegisterProbeTelemetry
{
   std::uint32_t sequence{};
   std::uint16_t raw{};
   std::uint8_t address{};
   std::uint8_t size{};
   bool available{};
   bool complete{};
};

struct TelemetrySnapshot
{
   std::uint64_t generation{};
   std::uint64_t updated_ms{};
   std::uint64_t core_updated_ms{};
   bool connected{};
   bool core_valid{};
   float input_voltage{};
   float input_current{};
   float input_power{};
   float total_output_power{};
   float native_total_output_power{};
   float rated_output_power{};
   std::uint16_t input_power_accumulator{};
   std::uint8_t input_power_rollover_count{};
   std::uint32_t input_power_sample_count{};
   float temperature_1{};
   float temperature_2{};
   float fan_rpm{};
   std::uint32_t uptime_seconds{};
   std::uint32_t ax_full_link_resets{};
   std::uint32_t ax_local_resyncs{};
   std::uint32_t ax_exchange_failures{};
   std::uint32_t ax_header_ack_mismatches{};
   std::uint32_t ax_trigger_ack_mismatches{};
   std::uint32_t ax_empty_reply_retries{};
   std::array<RailTelemetry, 3> rails{};
   std::array<RailTelemetry, 12> branches{};
   RegisterProbeTelemetry probe{};
   std::array<char, 16> manufacturer{'C', 'o', 'r', 's', 'a', 'i', 'r'};
   std::array<char, 24> model{'A', 'X', '1', '6', '0', '0', 'i'};
   std::array<char, 16> revision{'U', 'S', 'B', '-', 'P', 'M', 'B', 'u', 's'};
   std::array<char, 17> serial{};
};

} // namespace cusb2pmbus
