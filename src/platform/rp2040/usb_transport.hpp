#pragma once

#include "corsair_usb2pmbus/platform/usb_transport.hpp"
#include "tusb.h"

#include <cstddef>
#include <cstdint>

namespace cusb2pmbus::rp2040
{

struct UsbHostDiagnostics
{
   std::uint16_t vid;
   std::uint16_t pid;
   std::uint32_t open_attempts;
   std::uint8_t mounted_address;
   std::uint8_t interface_number;
   std::uint8_t interface_class;
   std::uint8_t reject_code;
   bool host_configured;
   bool host_initialized;
   bool transport_configured;
   TransferStatus transfer_status;
   std::uint32_t set_config_attempts;
   std::uint32_t controls_started;
   std::uint32_t controls_completed;
   std::uint32_t writes_started;
   std::uint32_t reads_started;
   std::uint32_t reads_completed;
   std::uint32_t last_read_size;
   std::uint8_t last_read_bytes[4];
   std::uint8_t last_endpoint;
   std::uint8_t last_result;
};

class UsbHostTransport final : public UsbTransport
{
public:
   static UsbHostTransport& instance();

   bool connected() const override
   {
      return configured_;
   }
   bool start_control(const ControlRequest& request) override;
   bool start_write(std::span<const std::uint8_t> data) override;
   bool start_read(std::span<std::uint8_t> data) override;
   TransferStatus transfer_status() const override
   {
      return status_;
   }
   std::size_t transferred() const override
   {
      return transferred_;
   }
   void clear_transfer() override;
   void abort_transfer() override;

   bool driver_init();
   bool driver_deinit();
   std::uint16_t driver_open(std::uint8_t rhport, std::uint8_t dev_addr,
                             const tusb_desc_interface_t* interface, std::uint16_t max_len);
   bool driver_set_config(std::uint8_t dev_addr, std::uint8_t interface_number);
   bool driver_transfer(std::uint8_t dev_addr, std::uint8_t endpoint, xfer_result_t result,
                        std::uint32_t transferred);
   void driver_close(std::uint8_t dev_addr);
   void control_complete(tuh_xfer_t* transfer);
   void note_mount(std::uint8_t dev_addr);
   void note_unmount(std::uint8_t dev_addr);
   void note_host_init(bool configured, bool initialized);
   UsbHostDiagnostics diagnostics() const;

private:
   UsbHostTransport() = default;
   std::uint8_t device_address_{};
   std::uint8_t interface_number_{};
   std::uint8_t endpoint_in_{};
   std::uint8_t endpoint_out_{};
   bool configured_{};
   volatile TransferStatus status_{TransferStatus::Idle};
   volatile std::size_t transferred_{};
   volatile std::uint16_t last_vid_{};
   volatile std::uint16_t last_pid_{};
   volatile std::uint32_t open_attempts_{};
   volatile std::uint8_t mounted_address_{};
   volatile std::uint8_t last_interface_number_{};
   volatile std::uint8_t last_interface_class_{};
   volatile std::uint8_t reject_code_{};
   volatile bool host_configured_{};
   volatile bool host_initialized_{};
   volatile std::uint32_t set_config_attempts_{};
   volatile std::uint32_t controls_started_{};
   volatile std::uint32_t controls_completed_{};
   volatile std::uint32_t writes_started_{};
   volatile std::uint32_t reads_started_{};
   volatile std::uint32_t reads_completed_{};
   volatile std::uint32_t last_read_size_{};
   volatile std::uint8_t last_read_bytes_[4]{};
   volatile std::uint8_t last_endpoint_{};
   volatile std::uint8_t last_result_{};
   volatile std::uint8_t active_endpoint_{};
   std::uint8_t* read_buffer_{};
   alignas(4) std::uint8_t setup_storage_[8]{};
};

} // namespace cusb2pmbus::rp2040
