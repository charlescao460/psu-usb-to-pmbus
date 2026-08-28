#pragma once

#include "corsair_usb2pmbus/pmbus/commands.hpp"
#include "corsair_usb2pmbus/telemetry/store.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cusb2pmbus::pmbus
{

enum class WriteResult : std::uint8_t
{
   Accepted,
   InvalidData,
   ReadOnly,
   Unsupported
};

struct ReadResult
{
   std::array<std::uint8_t, 40> bytes{};
   std::size_t size{};
   bool supported{};
};

class PmbusServer
{
public:
   explicit PmbusServer(const TelemetryStore& telemetry, std::uint8_t address = 0x58)
      : telemetry_(telemetry), address_(address)
   {
   }

   ReadResult read(std::uint8_t command, std::span<const std::uint8_t> request,
                   std::uint64_t now_ms);
   WriteResult write(std::uint8_t command, std::span<const std::uint8_t> payload);
   static bool write_payload_is_well_formed(std::uint8_t command,
                                            std::span<const std::uint8_t> payload);
   void note_bad_pec();
   std::uint8_t page() const
   {
      return page_;
   }
   std::uint8_t address() const
   {
      return address_;
   }

private:
   ReadResult read_on_page(std::uint8_t page, std::uint8_t command,
                           const TelemetrySnapshot& snapshot, bool stale);
   static const RailTelemetry* rail_for_page(const TelemetrySnapshot& snapshot, std::uint8_t page);
   static std::size_t smalert_mask_index(std::uint8_t status_command);
   static void put_word(ReadResult& response, std::uint16_t value);
   template <std::size_t N>
   static void put_block(ReadResult& response, const std::array<char, N>& value);

   const TelemetryStore& telemetry_;
   std::uint8_t address_;
   std::uint8_t page_{0};
   std::uint8_t pending_cml_{};
   std::array<std::uint8_t, 6> smalert_masks_{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
};

} // namespace cusb2pmbus::pmbus
