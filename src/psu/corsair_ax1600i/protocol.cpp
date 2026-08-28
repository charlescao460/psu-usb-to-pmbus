#include "corsair_usb2pmbus/psu/corsair_ax1600i/protocol.hpp"

#include <array>

namespace cusb2pmbus::ax1600i
{
namespace
{
constexpr std::array<std::uint8_t, 16> kEncodeTable{0x55, 0x56, 0x59, 0x5A, 0x65, 0x66, 0x69, 0x6A,
                                                    0x95, 0x96, 0x99, 0x9A, 0xA5, 0xA6, 0xA9, 0xAA};

int decode_nibble(std::uint8_t value)
{
   for (std::size_t i = 0; i < kEncodeTable.size(); ++i)
   {
      if (kEncodeTable[i] == value)
      {
         return static_cast<int>(i);
      }
   }
   return -1;
}

int decode_header(std::uint8_t value)
{
   for (std::size_t i = 0; i < kEncodeTable.size(); ++i)
   {
      if (static_cast<std::uint8_t>(kEncodeTable[i] & 0xFCU) == value)
      {
         return static_cast<int>(i);
      }
   }
   return -1;
}
} // namespace

std::size_t encoded_size(std::size_t payload_size)
{
   return payload_size * 2U + 2U;
}

std::size_t encode(std::uint8_t command, std::span<const std::uint8_t> payload,
                   std::span<std::uint8_t> output)
{
   const auto required = encoded_size(payload.size());
   if (output.size() < required || command > 7U)
   {
      return 0;
   }
   output[0] = static_cast<std::uint8_t>(kEncodeTable[(command << 1U) & 0x0FU] & 0xFCU);
   std::size_t cursor = 1;
   for (const auto value : payload)
   {
      output[cursor++] = kEncodeTable[value & 0x0FU];
      output[cursor++] = kEncodeTable[(value >> 4U) & 0x0FU];
   }
   output[cursor] = 0;
   return required;
}

DecodeResult decode(std::span<const std::uint8_t> encoded, std::span<std::uint8_t> output)
{
   if (encoded.empty())
   {
      return {DecodeError::TooShort, 0};
   }
   if (encoded.back() != 0U)
   {
      return {DecodeError::MissingTerminator, 0};
   }
   // The bridge may emit a terminator-only empty record while it catches up
   // with the PSU-side transaction. The reference drivers accept this as an
   // empty decoded response and continue the register sequence.
   if (encoded.size() == 1U)
   {
      return {DecodeError::None, 0};
   }
   const auto header = decode_header(encoded.front());
   if (header < 0 || ((header & 0x0F) >> 1) != 7)
   {
      return {DecodeError::InvalidHeader, 0};
   }
   if ((encoded.size() & 1U) != 0U)
   {
      return {DecodeError::InvalidSymbol, 0};
   }
   // Corsair replies are asymmetric with requests: the zero terminator is also
   // decoded as the low nibble of a final zero byte. Thus a minimal two-byte
   // reply decodes to {0}, and every longer reply includes a trailing zero.
   const auto required = encoded.size() / 2U;
   if (output.size() < required)
   {
      return {DecodeError::OutputTooSmall, 0};
   }
   for (std::size_t i = 0; i + 1U < required; ++i)
   {
      const int low = decode_nibble(encoded[1U + i * 2U]);
      const int high = decode_nibble(encoded[2U + i * 2U]);
      if (low < 0 || high < 0)
      {
         return {DecodeError::InvalidSymbol, 0};
      }
      output[i] = static_cast<std::uint8_t>(low | (high << 4));
   }
   output[required - 1U] = 0;
   return {DecodeError::None, required};
}

bool is_zero_reply(std::span<const std::uint8_t> response)
{
   return response.size() == 1U && response[0] == 0U;
}

bool is_ok_reply(std::span<const std::uint8_t> response)
{
   return response.size() == 2U && response[0] == 0U;
}

} // namespace cusb2pmbus::ax1600i
