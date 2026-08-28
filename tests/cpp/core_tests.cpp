#include "corsair_usb2pmbus/pmbus/commands.hpp"
#include "corsair_usb2pmbus/pmbus/linear.hpp"
#include "corsair_usb2pmbus/pmbus/pec.hpp"
#include "corsair_usb2pmbus/pmbus/server.hpp"
#include "corsair_usb2pmbus/psu/corsair_ax1600i/protocol.hpp"
#include "corsair_usb2pmbus/telemetry/power.hpp"
#include "corsair_usb2pmbus/telemetry/store.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
int failures = 0;

void check(bool condition, const char* description)
{
   if (!condition)
   {
      std::cerr << "FAIL: " << description << '\n';
      ++failures;
   }
}

void test_corsair_codec()
{
   constexpr std::array<std::uint8_t, 7> payload{0x13, 0x03, 0x06, 0x01, 0x07, 0x02, 0x88};
   std::array<std::uint8_t, 32> encoded{};
   const auto size = cusb2pmbus::ax1600i::encode(0, payload, encoded);
   check(size == 16, "encoded size");
   check(encoded[0] == 0x54 && encoded[size - 1] == 0, "encoded framing");

   // Replies use command/reply header 7, represented by the table entry for nibble 14.
   encoded[0] = 0xA8;
   std::array<std::uint8_t, 16> decoded{};
   const auto result =
      cusb2pmbus::ax1600i::decode(std::span<const std::uint8_t>(encoded.data(), size), decoded);
   check(static_cast<bool>(result), "decode succeeds");
   check(result.size == payload.size() + 1U, "decoded reply size includes terminator byte");
   check(std::equal(payload.begin(), payload.end(), decoded.begin()), "codec round trip");
   check(decoded[payload.size()] == 0, "decoded reply terminator");

   constexpr std::array<std::uint8_t, 2> minimal_reply{0xA8, 0};
   const auto minimal_result = cusb2pmbus::ax1600i::decode(minimal_reply, decoded);
   check(static_cast<bool>(minimal_result) && minimal_result.size == 1U && decoded[0] == 0U,
         "minimal zero reply");

   constexpr std::array<std::uint8_t, 1> empty_reply{0};
   const auto empty_result = cusb2pmbus::ax1600i::decode(empty_reply, decoded);
   check(static_cast<bool>(empty_result) && empty_result.size == 0U,
         "terminator-only reply is empty");

   constexpr std::array<std::uint8_t, 1> zero_reply{0};
   constexpr std::array<std::uint8_t, 2> ok_reply{0, 0};
   constexpr std::array<std::uint8_t, 2> error_reply{1, 0};
   check(cusb2pmbus::ax1600i::is_zero_reply(zero_reply), "header zero reply accepted");
   check(!cusb2pmbus::ax1600i::is_zero_reply(ok_reply), "header reply size enforced");
   check(cusb2pmbus::ax1600i::is_ok_reply(ok_reply), "trigger OK reply accepted");
   check(!cusb2pmbus::ax1600i::is_ok_reply(zero_reply), "trigger reply size enforced");
   check(!cusb2pmbus::ax1600i::is_ok_reply(error_reply), "trigger error rejected");

   encoded[1] = 0xFF;
   check(!cusb2pmbus::ax1600i::decode(std::span<const std::uint8_t>(encoded.data(), size), decoded),
         "invalid symbol rejected");
}

void test_linear()
{
   using namespace cusb2pmbus::pmbus;
   for (const float value : {0.0F, 0.5F, 3.3F, 12.0F, 230.0F, 1600.0F, -2.0F})
   {
      const auto round_trip = decode_linear11(encode_linear11(value));
      check(std::fabs(round_trip - value) <= std::max(0.02F, std::fabs(value) * 0.01F),
            "LINEAR11 round trip");
   }

   check(std::fabs(decode_linear16(encode_linear16(12.0F, -12), -12) - 12.0F) < 0.001F,
         "LINEAR16 round trip");
}

void test_pec()
{
   constexpr std::array<std::uint8_t, 9> bytes{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
   check(cusb2pmbus::pmbus::pec(bytes) == 0xF4, "CRC-8/SMBus known vector");

   constexpr std::array<std::uint8_t, 4> bmc_alert_mask{0xB0, 0x1B, 0x81, 0xFF};
   constexpr std::array<std::uint8_t, 3> bmc_mfr_d0{0xB0, 0xD0, 0x00};
   constexpr std::array<std::uint8_t, 7> bmc_page_plus_alert_mask{0xB0, 0x05, 0x04, 0x01,
                                                                  0x1B, 0x7C, 0xFF};
   check(cusb2pmbus::pmbus::pec(bmc_alert_mask) == 0x86, "captured BMC SMBALERT_MASK PEC");
   check(cusb2pmbus::pmbus::pec(bmc_mfr_d0) == 0x50, "captured BMC MFR_D0 PEC");
   check(cusb2pmbus::pmbus::pec(bmc_page_plus_alert_mask) == 0x0F,
         "captured BMC PAGE_PLUS_WRITE PEC");
}

void test_icue_power_derivation()
{
   cusb2pmbus::TelemetrySnapshot snapshot{};
   snapshot.native_total_output_power = 999.0F;
   snapshot.rails[0] = {12.41F, 34.25F, 999.0F, 0.0F, true, 444.0F};
   snapshot.rails[1] = {4.94F, 3.38F, 999.0F, 0.0F, true, 17.0F};
   snapshot.rails[2] = {3.28F, 3.00F, 999.0F, 0.0F, true, 0.0F};

   cusb2pmbus::update_derived_output_power(snapshot);

   check(std::fabs(snapshot.rails[0].power - 425.0425F) < 0.001F,
         "12 V power follows iCUE VOUT * IOUT");
   check(std::fabs(snapshot.rails[1].power - 16.6972F) < 0.001F,
         "5 V power follows iCUE VOUT * IOUT");
   check(std::fabs(snapshot.rails[2].power - 9.84F) < 0.001F,
         "3.3 V power follows iCUE VOUT * IOUT even when native POUT is zero");
   check(std::fabs(snapshot.total_output_power - 451.5797F) < 0.002F,
         "total output power is the sum of aggregate rail products");
   check(snapshot.native_total_output_power == 999.0F && snapshot.rails[2].native_power == 0.0F,
         "native diagnostic power remains separate");
}

void test_read_ein_accumulator()
{
   cusb2pmbus::TelemetrySnapshot snapshot{};
   cusb2pmbus::accumulate_input_power_sample(snapshot, 400.4F);
   cusb2pmbus::accumulate_input_power_sample(snapshot, 500.6F);
   check(snapshot.input_power_accumulator == 901U, "READ_EIN accumulates rounded watt samples");
   check(snapshot.input_power_rollover_count == 0U && snapshot.input_power_sample_count == 2U,
         "READ_EIN counters begin aligned");

   snapshot.input_power_accumulator = 32700U;
   snapshot.input_power_rollover_count = 0xFEU;
   snapshot.input_power_sample_count = 0xFFFFFEU;
   cusb2pmbus::accumulate_input_power_sample(snapshot, 100.0F);
   check(snapshot.input_power_accumulator == 33U && snapshot.input_power_rollover_count == 0xFFU &&
            snapshot.input_power_sample_count == 0xFFFFFFU,
         "READ_EIN rolls Paccum at 0x7FFF and advances aligned counters");
   cusb2pmbus::accumulate_input_power_sample(snapshot, 32767.0F);
   check(snapshot.input_power_accumulator == 33U && snapshot.input_power_rollover_count == 0U &&
            snapshot.input_power_sample_count == 0U,
         "READ_EIN rollover and 24-bit sample counters wrap independently");

   const auto saved = snapshot;
   cusb2pmbus::accumulate_input_power_sample(snapshot, std::numeric_limits<float>::quiet_NaN());
   cusb2pmbus::accumulate_input_power_sample(snapshot, -1.0F);
   check(snapshot.input_power_accumulator == saved.input_power_accumulator &&
            snapshot.input_power_rollover_count == saved.input_power_rollover_count &&
            snapshot.input_power_sample_count == saved.input_power_sample_count,
         "invalid input-power samples do not desynchronize READ_EIN");
}

void test_store_and_server()
{
   using namespace cusb2pmbus;
   using pmbus::Command;
   TelemetryStore store;
   TelemetrySnapshot snapshot{};
   snapshot.connected = true;
   snapshot.core_valid = true;
   snapshot.updated_ms = 1000;
   snapshot.core_updated_ms = 1000;
   snapshot.input_voltage = 120.0F;
   snapshot.input_current = 2.0F;
   snapshot.input_power = 240.0F;
   snapshot.rated_output_power = 1600.0F;
   snapshot.input_power_accumulator = 1234U;
   snapshot.input_power_rollover_count = 5U;
   snapshot.input_power_sample_count = 0x010203U;
   snapshot.serial = {'E', '6', '6', '1', '4', 'C', '3', '1',
                      '7', '6', '5', 'B', '9', 'A', '2', '0'};
   snapshot.rails[0] = {12.0F, 20.0F, 240.0F, 40.0F, true};
   snapshot.branches[0] = {12.0F, 5.0F, 60.0F, 10.0F, true};
   store.publish(snapshot);
   check(store.read().input_voltage == 120.0F, "snapshot publication");

   pmbus::PmbusServer server(store);
   auto result = server.read(static_cast<std::uint8_t>(Command::ReadVin), {}, 1500);
   check(result.supported && result.size == 2, "READ_VIN supported");
   const auto vin_raw = static_cast<std::uint16_t>(result.bytes[0] | (result.bytes[1] << 8));
   check(std::fabs(pmbus::decode_linear11(vin_raw) - 120.0F) < 1.0F, "READ_VIN value");

   const std::array<std::uint8_t, 1> branch_page{0x10};
   check(server.write(static_cast<std::uint8_t>(Command::Page), branch_page) ==
            pmbus::WriteResult::Accepted,
         "valid PAGE accepted");
   result = server.read(static_cast<std::uint8_t>(Command::ReadIout), {}, 1500);
   const auto current_raw = static_cast<std::uint16_t>(result.bytes[0] | (result.bytes[1] << 8));
   check(std::fabs(pmbus::decode_linear11(current_raw) - 5.0F) < 0.1F, "branch page mapping");

   const std::array<std::uint8_t, 1> query_request{static_cast<std::uint8_t>(Command::ReadVin)};
   result = server.read(static_cast<std::uint8_t>(Command::Query), query_request, 1500);
   check(result.supported && result.size == 1 && result.bytes[0] == 0xA0U,
         "legacy QUERY reports supported, readable LINEAR data");

   const std::array<std::uint8_t, 2> counted_query_request{
      1, static_cast<std::uint8_t>(Command::ReadEin)};
   result = server.read(static_cast<std::uint8_t>(Command::Query), counted_query_request, 1500);
   check(result.supported && result.size == 2 && result.bytes[0] == 1U && result.bytes[1] == 0xACU,
         "canonical QUERY process call reports READ_EIN Direct format");

   const std::array<std::uint8_t, 3> coefficients_request{
      2, static_cast<std::uint8_t>(Command::ReadEin), 1};
   result =
      server.read(static_cast<std::uint8_t>(Command::Coefficients), coefficients_request, 1500);
   check(result.supported && result.size == 6U && result.bytes[0] == 5U && result.bytes[1] == 1U &&
            result.bytes[2] == 0U && result.bytes[3] == 0U && result.bytes[4] == 0U &&
            result.bytes[5] == 0U,
         "COEFFICIENTS uses canonical counted process-call framing");

   result = server.read(static_cast<std::uint8_t>(Command::MfrSerial), {}, 1500);
   check(result.supported && result.size == 17 && result.bytes[0] == 16 && result.bytes[1] == 'E' &&
            result.bytes[16] == '0',
         "MFR_SERIAL returns the 16-digit RP2040 ID");

   result = server.read(static_cast<std::uint8_t>(Command::MfrPoutMax), {}, 1500);
   const auto pout_max_raw = static_cast<std::uint16_t>(result.bytes[0] | (result.bytes[1] << 8));
   check(result.supported && result.size == 2U &&
            std::fabs(pmbus::decode_linear11(pout_max_raw) - 1600.0F) < 1.0F,
         "MFR_POUT_MAX reports the PSU's rated DC output capacity");

   result = server.read(static_cast<std::uint8_t>(Command::ReadEin), {}, 1500);
   check(result.supported && result.size == 7U && result.bytes[0] == 6U &&
            result.bytes[1] == 0xD2U && result.bytes[2] == 0x04U && result.bytes[3] == 5U &&
            result.bytes[4] == 0x03U && result.bytes[5] == 0x02U && result.bytes[6] == 0x01U,
         "READ_EIN returns Paccum, rollover, and aligned 24-bit sample count");

   const std::array<std::uint8_t, 3> page_plus_request{
      2, 0, static_cast<std::uint8_t>(Command::StatusWord)};
   result = server.read(static_cast<std::uint8_t>(Command::PagePlusRead), page_plus_request, 1500);
   check(result.supported && result.size == 3 && result.bytes[0] == 2,
         "PAGE_PLUS_READ returns counted response");

   const std::array<std::uint8_t, 2> bmc_alert_mask{
      static_cast<std::uint8_t>(Command::StatusFans12), 0xFF};
   check(server.write(static_cast<std::uint8_t>(Command::SmAlertMask), bmc_alert_mask) ==
            pmbus::WriteResult::Accepted,
         "captured BMC SMBALERT_MASK write accepted");
   const std::array<std::uint8_t, 2> alert_mask_readback{
      1, static_cast<std::uint8_t>(Command::StatusFans12)};
   result = server.read(static_cast<std::uint8_t>(Command::SmAlertMask), alert_mask_readback, 1500);
   check(result.supported && result.size == 2 && result.bytes[0] == 1 && result.bytes[1] == 0xFF,
         "SMBALERT_MASK process-call readback");

   const std::array<std::uint8_t, 5> bmc_page_plus_alert_mask{
      4, 1, static_cast<std::uint8_t>(Command::SmAlertMask),
      static_cast<std::uint8_t>(Command::StatusInput), 0xFF};
   check(server.write(static_cast<std::uint8_t>(Command::PagePlusWrite),
                      bmc_page_plus_alert_mask) == pmbus::WriteResult::Accepted,
         "captured BMC PAGE_PLUS_WRITE alert mask accepted");
   const std::array<std::uint8_t, 2> input_mask_readback{
      1, static_cast<std::uint8_t>(Command::StatusInput)};
   result = server.read(static_cast<std::uint8_t>(Command::SmAlertMask), input_mask_readback, 1500);
   check(result.supported && result.bytes[1] == 0xFF, "PAGE_PLUS_WRITE updates local alert mask");

   constexpr std::array<std::uint8_t, 1> bmc_mfr_d0{0x00};
   check(server.write(static_cast<std::uint8_t>(Command::MfrBridgeStatus), bmc_mfr_d0) ==
            pmbus::WriteResult::Accepted,
         "captured BMC MFR_D0 zero write accepted as a no-op");
   result = server.read(static_cast<std::uint8_t>(Command::StatusWord), {}, 1500);
   check(result.supported && result.bytes[0] == 0 && result.bytes[1] == 0,
         "BMC compatibility writes do not set CML");

   constexpr std::array<std::uint8_t, 1> all_pages{0xFF};
   check(server.write(static_cast<std::uint8_t>(Command::Page), all_pages) ==
               pmbus::WriteResult::Accepted &&
            server.page() == 0x10U,
         "PAGE=0xFF broadcast is accepted without breaking the readable page");

   const std::array<std::uint8_t, 1> invalid_page{0x0F};
   check(server.write(static_cast<std::uint8_t>(Command::Page), invalid_page) ==
            pmbus::WriteResult::InvalidData,
         "invalid PAGE rejected");
   result = server.read(static_cast<std::uint8_t>(Command::StatusWord), {}, 1500);
   auto status = static_cast<std::uint16_t>(result.bytes[0] | (result.bytes[1] << 8));
   check((status & 0x0002U) != 0U, "malformed known write reports a one-shot CML");
   result = server.read(static_cast<std::uint8_t>(Command::StatusWord), {}, 1500);
   status = static_cast<std::uint16_t>(result.bytes[0] | (result.bytes[1] << 8));
   check((status & 0x0002U) == 0U, "CML auto-clears after one status observation");

   const std::array<std::uint8_t, 2> fan_write{0, 0};
   check(server.write(static_cast<std::uint8_t>(Command::FanCommand1), fan_write) ==
            pmbus::WriteResult::ReadOnly,
         "fan writes rejected");
   result = server.read(static_cast<std::uint8_t>(Command::StatusWord), {}, 1500);
   status = static_cast<std::uint16_t>(result.bytes[0] | (result.bytes[1] << 8));
   check((status & 0x0002U) == 0U, "safely rejected control write does not poison CML");

   constexpr std::array<std::uint8_t, 2> unknown_mfr_payload{0x12, 0x34};
   check(server.write(0xC4U, unknown_mfr_payload) == pmbus::WriteResult::Accepted &&
            server.write(0xD1U, unknown_mfr_payload) == pmbus::WriteResult::Accepted,
         "manufacturer-specific discovery writes are accepted as local no-ops");
   check(!server.read(0x55U, {}, 1500).supported, "unknown reads remain unsupported");
   result = server.read(static_cast<std::uint8_t>(Command::StatusWord), {}, 1500);
   status = static_cast<std::uint16_t>(result.bytes[0] | (result.bytes[1] << 8));
   check((status & 0x0002U) == 0U, "unknown discovery commands do not latch CML");

   server.note_bad_pec();
   result = server.read(static_cast<std::uint8_t>(Command::StatusCml), {}, 1500);
   check(result.bytes[0] == 0x20U, "bad PEC reports packet-error CML once");
   result = server.read(static_cast<std::uint8_t>(Command::StatusCml), {}, 1500);
   check(result.bytes[0] == 0U, "STATUS_CML consumes the transient diagnostic");

   server.note_bad_pec();
   constexpr std::array<std::uint8_t, 1> clear_cml{0x20};
   check(server.write(static_cast<std::uint8_t>(Command::StatusCml), clear_cml) ==
            pmbus::WriteResult::Accepted,
         "STATUS_CML clear writes are handled locally");
   check(server.write(static_cast<std::uint8_t>(Command::ClearFaults), {}) ==
            pmbus::WriteResult::Accepted,
         "command-only CLEAR_FAULTS is accepted");

   check(pmbus::PmbusServer::write_payload_is_well_formed(static_cast<std::uint8_t>(Command::Page),
                                                          branch_page),
         "well-formed PAGE can be accepted without PEC");
   check(!pmbus::PmbusServer::write_payload_is_well_formed(static_cast<std::uint8_t>(Command::Page),
                                                           fan_write),
         "ambiguous malformed PAGE cannot bypass PEC validation");

   result = server.read(static_cast<std::uint8_t>(Command::ReadVin), {}, 4001);
   auto stale_raw = static_cast<std::uint16_t>(result.bytes[0] | (result.bytes[1] << 8));
   check(stale_raw != 0, "last telemetry is preserved inside the ten-second USB grace period");
   result = server.read(static_cast<std::uint8_t>(Command::ReadVin), {}, 11001);
   stale_raw = static_cast<std::uint16_t>(result.bytes[0] | (result.bytes[1] << 8));
   check(stale_raw == 0, "stale telemetry is zero");
   result = server.read(static_cast<std::uint8_t>(Command::StatusWord), {}, 11001);
   status = static_cast<std::uint16_t>(result.bytes[0] | (result.bytes[1] << 8));
   check(status == 0x0040U, "stale USB telemetry reports OFF without INPUT or CML");
   result = server.read(static_cast<std::uint8_t>(Command::StatusInput), {}, 11001);
   check(result.bytes[0] == 0U, "stale USB telemetry does not fabricate an AC-input fault");
}
} // namespace

int main()
{
   test_corsair_codec();
   test_linear();
   test_pec();
   test_icue_power_derivation();
   test_read_ein_accumulator();
   test_store_and_server();
   if (failures != 0)
   {
      std::cerr << failures << " test(s) failed\n";
      return EXIT_FAILURE;
   }
   std::cout << "All core tests passed\n";
   return EXIT_SUCCESS;
}
