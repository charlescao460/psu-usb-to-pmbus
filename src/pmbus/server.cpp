#include "psu_usb_to_pmbus/pmbus/server.hpp"

#include "psu_usb_to_pmbus/pmbus/linear.hpp"

#include <algorithm>
#include <cmath>

namespace psu_usb_to_pmbus::pmbus
{
namespace
{
constexpr std::uint16_t kStatusOff = 1U << 6U;
constexpr std::uint16_t kStatusCml = 1U << 1U;
constexpr std::uint8_t kStatusCmlInvalidData = 1U << 6U;
constexpr std::uint8_t kStatusCmlPacketError = 1U << 5U;
constexpr std::int8_t kVoutExponent = -12;
constexpr std::uint64_t kUsbTelemetryGraceMs = 10000;
constexpr std::array<std::uint8_t, 6> kSmAlertStatusCommands{
   static_cast<std::uint8_t>(Command::StatusByte),
   static_cast<std::uint8_t>(Command::StatusIout),
   static_cast<std::uint8_t>(Command::StatusInput),
   static_cast<std::uint8_t>(Command::StatusTemperature),
   static_cast<std::uint8_t>(Command::StatusCml),
   static_cast<std::uint8_t>(Command::StatusFans12),
};

std::size_t text_length(const char* data, std::size_t capacity)
{
   std::size_t length = 0;
   while (length < capacity && data[length] != '\0')
   {
      ++length;
   }
   return length;
}

std::uint8_t query_data_format(std::uint8_t command)
{
   switch (static_cast<Command>(command))
   {
   case Command::ReadEin:
      return 0x0CU; // Direct format (QUERY bits 4:2 = 011).
   case Command::Page:
      return 0x10U; // 8-bit unsigned (QUERY bits 4:2 = 100).
   case Command::ReadVin:
   case Command::ReadIin:
   case Command::ReadVout:
   case Command::ReadIout:
   case Command::ReadTemperature1:
   case Command::ReadTemperature2:
   case Command::ReadFanSpeed1:
   case Command::ReadPout:
   case Command::ReadPin:
   case Command::MfrIoutMax:
   case Command::MfrPoutMax:
   case Command::MfrBranchOcp:
   case Command::MfrMaxTemp1:
   case Command::MfrMaxTemp2:
      return 0x00U; // LINEAR11 or ULINEAR16.
   default:
      return 0x1CU; // Non-numeric, bit field, or block data.
   }
}

bool query_is_special_read(std::uint8_t command)
{
   return command == static_cast<std::uint8_t>(Command::Query) ||
          command == static_cast<std::uint8_t>(Command::PagePlusRead);
}

bool query_is_locally_writable(std::uint8_t command)
{
   switch (static_cast<Command>(command))
   {
   case Command::Page:
   case Command::ClearFaults:
   case Command::PagePlusWrite:
   case Command::SmAlertMask:
   case Command::StatusByte:
   case Command::StatusWord:
   case Command::StatusIout:
   case Command::StatusInput:
   case Command::StatusTemperature:
   case Command::StatusCml:
   case Command::StatusFans12:
      return true;
   default:
      return false;
   }
}
} // namespace

std::size_t PmbusServer::smalert_mask_index(std::uint8_t status_command)
{
   const auto found =
      std::find(kSmAlertStatusCommands.begin(), kSmAlertStatusCommands.end(), status_command);
   return static_cast<std::size_t>(found - kSmAlertStatusCommands.begin());
}

void PmbusServer::put_word(ReadResult& response, std::uint16_t value)
{
   response.bytes[0] = static_cast<std::uint8_t>(value & 0xFFU);
   response.bytes[1] = static_cast<std::uint8_t>(value >> 8U);
   response.size = 2;
   response.supported = true;
}

template <std::size_t N>
void PmbusServer::put_block(ReadResult& response, const std::array<char, N>& value)
{
   const auto length = std::min<std::size_t>(text_length(value.data(), N), 32U);
   response.bytes[0] = static_cast<std::uint8_t>(length);
   for (std::size_t i = 0; i < length; ++i)
   {
      response.bytes[i + 1U] = static_cast<std::uint8_t>(value[i]);
   }
   response.size = length + 1U;
   response.supported = true;
}

const RailTelemetry* PmbusServer::rail_for_page(const TelemetrySnapshot& snapshot,
                                                std::uint8_t page)
{
   if (page <= 2U)
   {
      return &snapshot.rails[page];
   }
   if (page >= 0x10U && page <= 0x1BU)
   {
      return &snapshot.branches[page - 0x10U];
   }
   return nullptr;
}

ReadResult PmbusServer::read(std::uint8_t command, std::span<const std::uint8_t> request,
                             std::uint64_t now_ms)
{
   const auto snapshot = telemetry_.read();
   // Availability changes are intentionally debounced. A detach, partial
   // refresh, or transient USB failure keeps serving the last complete core
   // snapshot for ten seconds before PMBus transitions to zero/OFF.
   const bool stale =
      now_ms < snapshot.core_updated_ms || now_ms - snapshot.core_updated_ms > kUsbTelemetryGraceMs;

   if (command == static_cast<std::uint8_t>(Command::SmAlertMask) && !request.empty())
   {
      // SMBALERT_MASK readback is a block-write/block-read process call. The
      // request block is [count=1, STATUS_x], and the response is [count=1, mask].
      if (request.size() != 2U || request[0] != 1U)
      {
         pending_cml_ |= kStatusCmlInvalidData;
         return {};
      }
      const auto index = smalert_mask_index(request[1]);
      if (index >= smalert_masks_.size())
      {
         pending_cml_ |= kStatusCmlInvalidData;
         return {};
      }
      ReadResult response{};
      response.bytes[0] = 1;
      response.bytes[1] = smalert_masks_[index];
      response.size = 2;
      response.supported = true;
      return response;
   }

   if (command == static_cast<std::uint8_t>(Command::Query))
   {
      const bool counted = request.size() == 2U && request[0] == 1U;
      if (!counted && request.size() != 1U)
      {
         pending_cml_ |= kStatusCmlInvalidData;
         return {};
      }
      const auto queried_command = counted ? request[1] : request[0];
      const auto saved_cml = pending_cml_;
      const auto probe = read_on_page(page_, queried_command, snapshot, stale);
      pending_cml_ = saved_cml; // QUERY must not consume a pending one-shot status.
      ReadResult response{};
      const bool readable = probe.supported || query_is_special_read(queried_command);
      const bool writable = query_is_locally_writable(queried_command);
      const auto query =
         readable || writable
            ? static_cast<std::uint8_t>(0x80U | (writable ? 0x40U : 0U) | (readable ? 0x20U : 0U) |
                                        query_data_format(queried_command))
            : 0x00U;
      if (counted)
      {
         response.bytes[0] = 1U;
         response.bytes[1] = query;
         response.size = 2U;
      }
      else
      {
         response.bytes[0] = query;
         response.size = 1U;
      }
      response.supported = true;
      return response;
   }

   if (command == static_cast<std::uint8_t>(Command::Coefficients) && !request.empty())
   {
      const bool counted = request.size() == 3U && request[0] == 2U;
      const bool legacy = request.size() == 2U;
      const auto offset = counted ? 1U : 0U;
      if ((!counted && !legacy) || request[offset] != static_cast<std::uint8_t>(Command::ReadEin) ||
          request[offset + 1U] != 1U)
      {
         pending_cml_ |= kStatusCmlInvalidData;
         return {};
      }
      ReadResult response{};
      if (counted)
      {
         response.bytes = {5, 1, 0, 0, 0, 0};
         response.size = 6U;
      }
      else
      {
         response.bytes = {1, 0, 0, 0, 0};
         response.size = 5U;
      }
      response.supported = true;
      return response;
   }

   if (command == static_cast<std::uint8_t>(Command::PagePlusRead))
   {
      // PMBus PAGE_PLUS_READ request block is [count=2, page, command].
      if (request.size() != 3U || request[0] != 2U || !is_valid_page(request[1]))
      {
         pending_cml_ |= kStatusCmlInvalidData;
         return {};
      }
      auto nested = read_on_page(request[1], request[2], snapshot, stale);
      if (!nested.supported || nested.size >= nested.bytes.size())
      {
         return {};
      }
      for (std::size_t i = nested.size; i > 0; --i)
      {
         nested.bytes[i] = nested.bytes[i - 1U];
      }
      nested.bytes[0] = static_cast<std::uint8_t>(nested.size);
      ++nested.size;
      return nested;
   }
   return read_on_page(page_, command, snapshot, stale);
}

ReadResult PmbusServer::read_on_page(std::uint8_t page, std::uint8_t command,
                                     const TelemetrySnapshot& snapshot, bool stale)
{
   ReadResult response{};
   const auto* rail = rail_for_page(snapshot, page);
   const auto cmd = static_cast<Command>(command);
   const auto cml = pending_cml_;
   const auto status =
      static_cast<std::uint16_t>((cml != 0U ? kStatusCml : 0U) | (stale ? kStatusOff : 0U));

   switch (cmd)
   {
   case Command::Page:
      response.bytes[0] = page;
      response.size = 1;
      response.supported = true;
      break;
   case Command::Capability:
      response.bytes[0] = 0x80; // PEC supported, 100 kHz, no SMBALERT# response.
      response.size = 1;
      response.supported = true;
      break;
   case Command::VoutMode:
      response.bytes[0] = static_cast<std::uint8_t>(kVoutExponent) & 0x1FU;
      response.size = 1;
      response.supported = true;
      break;
   case Command::PmbusRevision:
      response.bytes[0] = 0x22; // PMBus 1.2 command set.
      response.size = 1;
      response.supported = true;
      break;
   case Command::AppProfileSupport:
      response.bytes[0] = 0x04; // AC/DC Server PSU application profile.
      response.size = 1;
      response.supported = true;
      break;
   case Command::FanConfig12:
      response.bytes[0] = 0x80; // Fan 1 installed; duty-cycle command mode.
      response.size = 1;
      response.supported = true;
      break;
   case Command::StatusByte:
      response.bytes[0] = static_cast<std::uint8_t>(status & 0xFFU);
      response.size = 1;
      response.supported = true;
      pending_cml_ = 0;
      break;
   case Command::StatusWord:
      put_word(response, status);
      pending_cml_ = 0;
      break;
   case Command::StatusInput:
      // USB telemetry loss is a bridge availability condition, not evidence of
      // missing AC input. Report OFF in STATUS_WORD and keep STATUS_INPUT clear.
      response.bytes[0] = 0x00;
      response.size = 1;
      response.supported = true;
      break;
   case Command::StatusCml:
      response.bytes[0] = cml;
      response.size = 1;
      response.supported = true;
      pending_cml_ = 0;
      break;
   case Command::StatusIout:
   case Command::StatusTemperature:
   case Command::StatusFans12:
      response.bytes[0] = 0;
      response.size = 1;
      response.supported = true;
      break;
   case Command::ReadVin:
      put_word(response, encode_linear11(stale ? 0.0F : snapshot.input_voltage));
      break;
   case Command::ReadIin:
      put_word(response, encode_linear11(stale ? 0.0F : snapshot.input_current));
      break;
   case Command::ReadPin:
      put_word(response, encode_linear11(stale ? 0.0F : snapshot.input_power));
      break;
   case Command::ReadVout:
      put_word(response,
               encode_linear16(stale || rail == nullptr ? 0.0F : rail->voltage, kVoutExponent));
      break;
   case Command::ReadIout:
      put_word(response, encode_linear11(stale || rail == nullptr ? 0.0F : rail->current));
      break;
   case Command::ReadPout:
      put_word(response, encode_linear11(stale || rail == nullptr ? 0.0F : rail->power));
      break;
   case Command::ReadTemperature1:
      put_word(response, encode_linear11(stale ? 0.0F : snapshot.temperature_1));
      break;
   case Command::ReadTemperature2:
      put_word(response, encode_linear11(stale ? 0.0F : snapshot.temperature_2));
      break;
   case Command::ReadFanSpeed1:
      put_word(response, encode_linear11(stale ? 0.0F : snapshot.fan_rpm));
      break;
   case Command::ReadEin:
   {
      // AC/DC Server Profile 1.2: Paccum is a two-byte Direct value (m=1,
      // b=0, R=0), followed by an 8-bit rollover count and an aligned 24-bit
      // sample count. This state is retained while ordinary telemetry is stale.
      response.bytes[0] = 6;
      response.bytes[1] = static_cast<std::uint8_t>(snapshot.input_power_accumulator & 0xFFU);
      response.bytes[2] = static_cast<std::uint8_t>(snapshot.input_power_accumulator >> 8U);
      response.bytes[3] = snapshot.input_power_rollover_count;
      response.bytes[4] = static_cast<std::uint8_t>(snapshot.input_power_sample_count & 0xFFU);
      response.bytes[5] =
         static_cast<std::uint8_t>((snapshot.input_power_sample_count >> 8U) & 0xFFU);
      response.bytes[6] =
         static_cast<std::uint8_t>((snapshot.input_power_sample_count >> 16U) & 0xFFU);
      response.size = 7;
      response.supported = true;
      break;
   }
   case Command::MfrId:
      put_block(response, snapshot.manufacturer);
      break;
   case Command::MfrModel:
      put_block(response, snapshot.model);
      break;
   case Command::MfrRevision:
      put_block(response, snapshot.revision);
      break;
   case Command::MfrSerial:
      put_block(response, snapshot.serial);
      break;
   case Command::MfrIoutMax:
      put_word(response, encode_linear11(rail == nullptr ? 0.0F : rail->ocp_limit));
      break;
   case Command::MfrPoutMax:
      // Static PSU identity/capacity data remains valid even when live USB
      // telemetry is stale. Server BMCs use this during cold-start discovery.
      put_word(response, encode_linear11(snapshot.rated_output_power));
      break;
   case Command::MfrBranchOcp:
      put_word(response, encode_linear11(rail == nullptr ? 0.0F : rail->ocp_limit));
      break;
   case Command::MfrMaxTemp1:
   case Command::MfrMaxTemp2:
      put_word(response, encode_linear11(105.0F));
      break;
   case Command::MfrUptime:
      put_word(response, static_cast<std::uint16_t>(snapshot.uptime_seconds & 0xFFFFU));
      break;
   case Command::MfrBridgeStatus:
      response.bytes[0] = snapshot.connected ? (stale ? 1U : 0U) : 2U;
      response.size = 1;
      response.supported = true;
      break;
   case Command::SmAlertMask:
      response.bytes[0] = smalert_masks_[0];
      response.size = 1;
      response.supported = true;
      break;
   case Command::Coefficients:
      // m=1, b=0, R=0: useful for hosts that request coefficients for linear data.
      response.bytes = {1, 0, 0, 0, 0};
      response.size = 5;
      response.supported = true;
      break;
   default:
      // Discovery code routinely probes optional commands. An unsupported read
      // remains unsupported, but does not poison subsequent telemetry status.
      break;
   }
   return response;
}

bool PmbusServer::write_payload_is_well_formed(std::uint8_t command,
                                               std::span<const std::uint8_t> payload)
{
   switch (static_cast<Command>(command))
   {
   case Command::Page:
      return payload.size() == 1U;
   case Command::PagePlusWrite:
      return payload.size() >= 3U && payload[0] == payload.size() - 1U;
   case Command::ClearFaults:
      return payload.empty();
   case Command::SmAlertMask:
   case Command::StatusWord:
   case Command::FanCommand1:
      return payload.size() == 2U;
   case Command::StatusByte:
   case Command::StatusIout:
   case Command::StatusInput:
   case Command::StatusTemperature:
   case Command::StatusCml:
   case Command::StatusFans12:
      return payload.size() == 1U;
   default:
      // Manufacturer-specific commands are intentionally local no-ops. They
      // cannot control the PSU, so accepting board-vendor discovery writes is
      // safer than interpreting their undocumented payloads.
      return command >= 0xC4U && command <= 0xFDU;
   }
}

WriteResult PmbusServer::write(std::uint8_t command, std::span<const std::uint8_t> payload)
{
   switch (static_cast<Command>(command))
   {
   case Command::Page:
      if (payload.size() != 1U || (!is_valid_page(payload[0]) && payload[0] != 0xFFU))
      {
         pending_cml_ |= kStatusCmlInvalidData;
         return WriteResult::InvalidData;
      }
      // PAGE=0xFF applies subsequent writes to every output. All writes here are
      // local, so accept it while preserving the last readable telemetry page.
      if (payload[0] != 0xFFU)
      {
         page_ = payload[0];
      }
      return WriteResult::Accepted;
   case Command::PagePlusWrite:
      // The request block is [count, page, command, data...]. Only local,
      // non-control operations are eligible; PAGE_PLUS_WRITE never reaches the PSU.
      if (payload.size() < 3U || payload[0] != payload.size() - 1U || !is_valid_page(payload[1]))
      {
         pending_cml_ |= kStatusCmlInvalidData;
         return WriteResult::InvalidData;
      }
      return write(payload[2], payload.subspan(3U));
   case Command::ClearFaults:
      if (!payload.empty())
      {
         pending_cml_ |= kStatusCmlInvalidData;
         return WriteResult::InvalidData;
      }
      pending_cml_ = 0;
      return WriteResult::Accepted;
   case Command::SmAlertMask:
   {
      // A mask only changes logical PMBus alert state; this target has no
      // physical SMBALERT# output. Never forward it to the PSU.
      if (payload.size() != 2U)
      {
         pending_cml_ |= kStatusCmlInvalidData;
         return WriteResult::InvalidData;
      }
      const auto index = smalert_mask_index(payload[0]);
      if (index >= smalert_masks_.size())
      {
         pending_cml_ |= kStatusCmlInvalidData;
         return WriteResult::InvalidData;
      }
      smalert_masks_[index] = payload[1];
      return WriteResult::Accepted;
   }
   case Command::StatusByte:
   case Command::StatusIout:
   case Command::StatusInput:
   case Command::StatusTemperature:
   case Command::StatusCml:
   case Command::StatusFans12:
      if (payload.size() != 1U)
      {
         pending_cml_ |= kStatusCmlInvalidData;
         return WriteResult::InvalidData;
      }
      pending_cml_ = 0;
      return WriteResult::Accepted;
   case Command::StatusWord:
      if (payload.size() != 2U)
      {
         pending_cml_ |= kStatusCmlInvalidData;
         return WriteResult::InvalidData;
      }
      pending_cml_ = 0;
      return WriteResult::Accepted;
   case Command::FanCommand1:
      // Safe refusal: acknowledge the local transaction but never apply it and
      // never leave a compatibility-breaking CML latch behind.
      return WriteResult::ReadOnly;
   default:
      if (command >= 0xC4U && command <= 0xFDU)
      {
         return WriteResult::Accepted;
      }
      return WriteResult::Unsupported;
   }
}

void PmbusServer::note_bad_pec()
{
   pending_cml_ |= kStatusCmlPacketError;
}

} // namespace psu_usb_to_pmbus::pmbus
