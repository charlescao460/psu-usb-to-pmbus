#include "i2c_target.hpp"

#include "corsair_usb2pmbus/pmbus/pec.hpp"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"
#include "pico/i2c_slave.h"
#include "pico/time.h"

#include <span>

namespace cusb2pmbus::rp2040
{
namespace
{
constexpr std::uint8_t kSdaPin = 2;
constexpr std::uint8_t kSclPin = 3;
constexpr std::uint64_t kTransactionTimeoutMs = 25;
I2cTarget* target_instance = nullptr;

void slave_handler(i2c_inst_t* instance, i2c_slave_event_t event)
{
   if (target_instance != nullptr)
      target_instance->event(instance, event);
}
} // namespace

void I2cTarget::initialize()
{
   target_instance = this;
   gpio_set_function(kSdaPin, GPIO_FUNC_I2C);
   gpio_set_function(kSclPin, GPIO_FUNC_I2C);
   gpio_disable_pulls(kSdaPin);
   gpio_disable_pulls(kSclPin);
   i2c_init(i2c1, 100000);
   i2c_slave_init(i2c1, server_.address(), slave_handler);
   reset_transaction();
}

void I2cTarget::reset_transaction()
{
   receive_size_ = 0;
   transmit_size_ = 0;
   transmit_index_ = 0;
   read_active_ = false;
   finish_pending_ = false;
   last_event_ms_ = 0;
}

void I2cTarget::prepare_read(std::uint64_t now_ms)
{
   transmit_size_ = 0;
   transmit_index_ = 0;
   read_active_ = true;
   if (receive_size_ == 0U)
   {
      transmit_[transmit_size_++] = 0xFF;
      return;
   }
   const auto response = server_.read(
      receive_[0], std::span<const std::uint8_t>(receive_.data() + 1U, receive_size_ - 1U), now_ms);
   if (!response.supported)
   {
      transmit_[transmit_size_++] = 0xFF;
      return;
   }
   for (std::size_t i = 0; i < response.size; ++i)
      transmit_[transmit_size_++] = response.bytes[i];

   std::uint8_t crc = 0;
   crc = pmbus::pec_update(crc, static_cast<std::uint8_t>(server_.address() << 1U));
   for (std::size_t i = 0; i < receive_size_; ++i)
      crc = pmbus::pec_update(crc, receive_[i]);
   crc = pmbus::pec_update(crc, static_cast<std::uint8_t>((server_.address() << 1U) | 1U));
   for (std::size_t i = 0; i < response.size; ++i)
      crc = pmbus::pec_update(crc, response.bytes[i]);
   transmit_[transmit_size_++] = crc;
}

void I2cTarget::finish_transaction()
{
   if (!read_active_ && receive_size_ == 1U)
   {
      // Send-byte commands such as CLEAR_FAULTS have no data or PEC byte.
      (void)server_.write(receive_[0], {});
   }
   else if (!read_active_ && receive_size_ >= 2U)
   {
      std::uint8_t crc = pmbus::pec_update(0, static_cast<std::uint8_t>(server_.address() << 1U));
      for (std::size_t i = 0; i + 1U < receive_size_; ++i)
         crc = pmbus::pec_update(crc, receive_[i]);
      const auto protected_payload =
         std::span<const std::uint8_t>(receive_.data() + 1U, receive_size_ - 2U);
      if (crc == receive_[receive_size_ - 1U])
      {
         (void)server_.write(receive_[0], protected_payload);
      }
      else
      {
         // PEC support does not require every controller to append PEC. Accept an
         // unprotected write only when the complete payload has an unambiguous,
         // command-appropriate shape; otherwise retain the bad-PEC diagnostic.
         const auto unprotected_payload =
            std::span<const std::uint8_t>(receive_.data() + 1U, receive_size_ - 1U);
         if (pmbus::PmbusServer::write_payload_is_well_formed(receive_[0], unprotected_payload))
         {
            (void)server_.write(receive_[0], unprotected_payload);
         }
         else
         {
            server_.note_bad_pec();
         }
      }
   }
   reset_transaction();
}

void I2cTarget::event(i2c_inst_t* instance, i2c_slave_event_t event_value)
{
   last_event_ms_ = to_ms_since_boot(get_absolute_time());
   switch (event_value)
   {
   case I2C_SLAVE_RECEIVE:
      if (finish_pending_)
         finish_transaction();
      while (i2c_get_read_available(instance) != 0U)
      {
         const auto value = i2c_read_byte_raw(instance);
         if (receive_size_ < receive_.size())
            receive_[receive_size_++] = value;
      }
      break;
   case I2C_SLAVE_REQUEST:
      finish_pending_ = false;
      if (!read_active_)
         prepare_read(last_event_ms_);
      i2c_write_byte_raw(instance,
                         transmit_index_ < transmit_size_ ? transmit_[transmit_index_++] : 0xFF);
      break;
   case I2C_SLAVE_FINISH:
      // The SDK reports both STOP and repeated START as FINISH. Delay a write
      // commit briefly so a following REQUEST can reuse the received command.
      if (read_active_)
         finish_transaction();
      else
         finish_pending_ = true;
      break;
   }
}

void I2cTarget::task(std::uint64_t now_ms)
{
   if (finish_pending_ && now_ms >= last_event_ms_ + 2U)
   {
      const auto interrupts = save_and_disable_interrupts();
      if (finish_pending_)
         finish_transaction();
      restore_interrupts(interrupts);
      return;
   }
   if (last_event_ms_ == 0U || now_ms < last_event_ms_ ||
       now_ms - last_event_ms_ <= kTransactionTimeoutMs)
   {
      return;
   }
   const auto interrupts = save_and_disable_interrupts();
   i2c_slave_deinit(i2c1);
   i2c_init(i2c1, 100000);
   i2c_slave_init(i2c1, server_.address(), slave_handler);
   reset_transaction();
   restore_interrupts(interrupts);
}

} // namespace cusb2pmbus::rp2040
