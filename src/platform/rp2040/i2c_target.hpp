#pragma once

#include "corsair_usb2pmbus/pmbus/server.hpp"
#include "pico/i2c_slave.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace cusb2pmbus::rp2040
{

class I2cTarget
{
public:
   explicit I2cTarget(pmbus::PmbusServer& server) : server_(server)
   {
   }
   void initialize();
   void task(std::uint64_t now_ms);
   void event(i2c_inst_t* instance, i2c_slave_event_t event);

private:
   void prepare_read(std::uint64_t now_ms);
   void finish_transaction();
   void reset_transaction();

   pmbus::PmbusServer& server_;
   std::array<std::uint8_t, 40> receive_{};
   std::array<std::uint8_t, 42> transmit_{};
   std::size_t receive_size_{};
   std::size_t transmit_size_{};
   std::size_t transmit_index_{};
   std::uint64_t last_event_ms_{};
   bool read_active_{};
   bool finish_pending_{};
};

} // namespace cusb2pmbus::rp2040
