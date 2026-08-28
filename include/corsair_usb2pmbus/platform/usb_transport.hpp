#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace cusb2pmbus
{

enum class TransferStatus : std::uint8_t
{
   Idle,
   Busy,
   Complete,
   Failed
};

struct ControlRequest
{
   std::uint8_t request_type;
   std::uint8_t request;
   std::uint16_t value;
   std::uint16_t index;
};

class UsbTransport
{
public:
   virtual ~UsbTransport() = default;
   virtual bool connected() const = 0;
   virtual bool start_control(const ControlRequest& request) = 0;
   virtual bool start_write(std::span<const std::uint8_t> data) = 0;
   virtual bool start_read(std::span<std::uint8_t> data) = 0;
   virtual TransferStatus transfer_status() const = 0;
   virtual std::size_t transferred() const = 0;
   virtual void clear_transfer() = 0;
   virtual void abort_transfer() = 0;
};

} // namespace cusb2pmbus
