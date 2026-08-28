#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace cusb2pmbus::ax1600i
{

inline constexpr std::uint16_t kVendorId = 0x1B1C;
inline constexpr std::uint16_t kProductId = 0x1C11;
inline constexpr std::uint8_t kInterface = 0;
inline constexpr std::uint8_t kBulkOutEndpoint = 0x02;
inline constexpr std::uint8_t kBulkInEndpoint = 0x82;

enum class DecodeError : std::uint8_t
{
   None,
   TooShort,
   MissingTerminator,
   InvalidHeader,
   InvalidSymbol,
   OutputTooSmall,
};

struct DecodeResult
{
   DecodeError error{};
   std::size_t size{};
   explicit operator bool() const
   {
      return error == DecodeError::None;
   }
};

std::size_t encoded_size(std::size_t payload_size);
std::size_t encode(std::uint8_t command, std::span<const std::uint8_t> payload,
                   std::span<std::uint8_t> output);
DecodeResult decode(std::span<const std::uint8_t> encoded, std::span<std::uint8_t> output);

// Register command acknowledgements include the decoded framing terminator.
// A header acknowledgement is {0}; a trigger acknowledgement is {0, 0}.
bool is_zero_reply(std::span<const std::uint8_t> response);
bool is_ok_reply(std::span<const std::uint8_t> response);

} // namespace cusb2pmbus::ax1600i
