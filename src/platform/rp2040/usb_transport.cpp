#include "usb_transport.hpp"

#include "psu_usb_to_pmbus/psu/corsair_ax1600i/protocol.hpp"

#include "host/usbh_pvt.h"
#include "tusb.h"

#include <cstring>

namespace psu_usb_to_pmbus::rp2040
{
namespace
{
UsbHostTransport* transport()
{
   return &UsbHostTransport::instance();
}

bool driver_init()
{
   return transport()->driver_init();
}
bool driver_deinit()
{
   return transport()->driver_deinit();
}
std::uint16_t driver_open(std::uint8_t rhport, std::uint8_t dev_addr,
                          const tusb_desc_interface_t* interface, std::uint16_t max_len)
{
   return transport()->driver_open(rhport, dev_addr, interface, max_len);
}
bool driver_set_config(std::uint8_t dev_addr, std::uint8_t interface_number)
{
   return transport()->driver_set_config(dev_addr, interface_number);
}
bool driver_transfer(std::uint8_t dev_addr, std::uint8_t endpoint, xfer_result_t result,
                     std::uint32_t transferred)
{
   return transport()->driver_transfer(dev_addr, endpoint, result, transferred);
}
void driver_close(std::uint8_t dev_addr)
{
   transport()->driver_close(dev_addr);
}
void control_complete_cb(tuh_xfer_t* transfer)
{
   transport()->control_complete(transfer);
}

const usbh_class_driver_t kDriver{"AX1600i",         driver_init,     driver_deinit, driver_open,
                                  driver_set_config, driver_transfer, driver_close};
} // namespace

UsbHostTransport& UsbHostTransport::instance()
{
   static UsbHostTransport value;
   return value;
}

bool UsbHostTransport::driver_init()
{
   configured_ = false;
   status_ = TransferStatus::Idle;
   return true;
}

bool UsbHostTransport::driver_deinit()
{
   configured_ = false;
   status_ = TransferStatus::Idle;
   return true;
}

std::uint16_t UsbHostTransport::driver_open(std::uint8_t, std::uint8_t dev_addr,
                                            const tusb_desc_interface_t* interface,
                                            std::uint16_t max_len)
{
   open_attempts_ = open_attempts_ + 1U;
   last_interface_number_ = interface->bInterfaceNumber;
   last_interface_class_ = interface->bInterfaceClass;
   reject_code_ = 0;
   std::uint16_t vid = 0;
   std::uint16_t pid = 0;
   if (!tuh_vid_pid_get(dev_addr, &vid, &pid))
   {
      reject_code_ = 1;
      return 0;
   }
   last_vid_ = vid;
   last_pid_ = pid;
   if (vid != ax1600i::kVendorId || pid != ax1600i::kProductId)
   {
      reject_code_ = 2;
      return 0;
   }
   if (interface->bInterfaceNumber != ax1600i::kInterface)
   {
      reject_code_ = 3;
      return 0;
   }

   const auto* descriptor = reinterpret_cast<const std::uint8_t*>(interface);
   std::uint16_t consumed = interface->bLength;
   std::uint8_t endpoint_count = 0;
   descriptor = tu_desc_next(descriptor);
   endpoint_in_ = 0;
   endpoint_out_ = 0;
   while (consumed < max_len && consumed + descriptor[0] <= max_len &&
          endpoint_count < interface->bNumEndpoints)
   {
      if (descriptor[0] == 0U)
      {
         reject_code_ = 4;
         return 0;
      }
      if (tu_desc_type(descriptor) == TUSB_DESC_ENDPOINT)
      {
         const auto* endpoint = reinterpret_cast<const tusb_desc_endpoint_t*>(descriptor);
         if (endpoint->bmAttributes.xfer != TUSB_XFER_BULK || !tuh_edpt_open(dev_addr, endpoint))
         {
            reject_code_ = 5;
            return 0;
         }
         if (tu_edpt_dir(endpoint->bEndpointAddress) == TUSB_DIR_IN)
            endpoint_in_ = endpoint->bEndpointAddress;
         else
            endpoint_out_ = endpoint->bEndpointAddress;
         ++endpoint_count;
      }
      consumed = static_cast<std::uint16_t>(consumed + descriptor[0]);
      descriptor = tu_desc_next(descriptor);
   }
   if (endpoint_in_ != ax1600i::kBulkInEndpoint || endpoint_out_ != ax1600i::kBulkOutEndpoint)
   {
      reject_code_ = 6;
      return 0;
   }
   device_address_ = dev_addr;
   interface_number_ = interface->bInterfaceNumber;
   configured_ = false;
   return consumed;
}

bool UsbHostTransport::driver_set_config(std::uint8_t dev_addr, std::uint8_t interface_number)
{
   set_config_attempts_ = set_config_attempts_ + 1U;
   if (dev_addr != device_address_ || interface_number != interface_number_)
      return false;
   configured_ = true;
   usbh_driver_set_config_complete(dev_addr, interface_number);
   return true;
}

bool UsbHostTransport::driver_transfer(std::uint8_t dev_addr, std::uint8_t endpoint,
                                       xfer_result_t result, std::uint32_t transferred)
{
   if (dev_addr != device_address_ || (endpoint != endpoint_in_ && endpoint != endpoint_out_))
   {
      return false;
   }
   last_endpoint_ = endpoint;
   last_result_ = static_cast<std::uint8_t>(result);
   transferred_ = transferred;
   if (endpoint == endpoint_in_)
   {
      reads_completed_ = reads_completed_ + 1U;
      last_read_size_ = transferred;
      const auto captured =
         transferred < sizeof(last_read_bytes_) ? transferred : sizeof(last_read_bytes_);
      for (std::size_t i = 0; i < captured; ++i)
         last_read_bytes_[i] = read_buffer_[i];
      for (std::size_t i = captured; i < sizeof(last_read_bytes_); ++i)
         last_read_bytes_[i] = 0;
   }
   status_ = result == XFER_RESULT_SUCCESS ? TransferStatus::Complete : TransferStatus::Failed;
   return true;
}

void UsbHostTransport::driver_close(std::uint8_t dev_addr)
{
   if (dev_addr != device_address_)
      return;
   configured_ = false;
   device_address_ = 0;
   endpoint_in_ = 0;
   endpoint_out_ = 0;
   transferred_ = 0;
   status_ = TransferStatus::Failed;
}

void UsbHostTransport::note_mount(std::uint8_t dev_addr)
{
   std::uint16_t vid = 0;
   std::uint16_t pid = 0;
   mounted_address_ = dev_addr;
   if (tuh_vid_pid_get(dev_addr, &vid, &pid))
   {
      last_vid_ = vid;
      last_pid_ = pid;
   }
}

void UsbHostTransport::note_unmount(std::uint8_t dev_addr)
{
   if (mounted_address_ == dev_addr)
      mounted_address_ = 0;
}

void UsbHostTransport::note_host_init(bool configured, bool initialized)
{
   host_configured_ = configured;
   host_initialized_ = initialized;
}

UsbHostDiagnostics UsbHostTransport::diagnostics() const
{
   return UsbHostDiagnostics{
      last_vid_,
      last_pid_,
      open_attempts_,
      mounted_address_,
      last_interface_number_,
      last_interface_class_,
      reject_code_,
      host_configured_,
      host_initialized_,
      configured_,
      status_,
      set_config_attempts_,
      controls_started_,
      controls_completed_,
      writes_started_,
      reads_started_,
      reads_completed_,
      last_read_size_,
      {last_read_bytes_[0], last_read_bytes_[1], last_read_bytes_[2], last_read_bytes_[3]},
      last_endpoint_,
      last_result_};
}

bool UsbHostTransport::start_control(const ControlRequest& request)
{
   if (!configured_ || status_ != TransferStatus::Idle)
      return false;
   auto* setup = reinterpret_cast<tusb_control_request_t*>(setup_storage_);
   setup->bmRequestType = request.request_type;
   setup->bRequest = request.request;
   setup->wValue = request.value;
   setup->wIndex = request.index;
   setup->wLength = 0;
   tuh_xfer_t transfer{};
   transfer.daddr = device_address_;
   transfer.ep_addr = 0;
   transfer.setup = setup;
   transfer.buffer = nullptr;
   transfer.complete_cb = control_complete_cb;
   transfer.user_data = 0;
   status_ = TransferStatus::Busy;
   active_endpoint_ = 0;
   if (!tuh_control_xfer(&transfer))
   {
      status_ = TransferStatus::Idle;
      return false;
   }
   controls_started_ = controls_started_ + 1U;
   return true;
}

void UsbHostTransport::control_complete(tuh_xfer_t* transfer)
{
   controls_completed_ = controls_completed_ + 1U;
   last_endpoint_ = 0;
   last_result_ = static_cast<std::uint8_t>(transfer->result);
   transferred_ = transfer->actual_len;
   status_ =
      transfer->result == XFER_RESULT_SUCCESS ? TransferStatus::Complete : TransferStatus::Failed;
}

bool UsbHostTransport::start_write(std::span<const std::uint8_t> data)
{
   if (!configured_ || status_ != TransferStatus::Idle || data.empty() || data.size() > 0xFFFFU ||
       !usbh_edpt_claim(device_address_, endpoint_out_))
   {
      return false;
   }
   status_ = TransferStatus::Busy;
   active_endpoint_ = endpoint_out_;
   if (!usbh_edpt_xfer(device_address_, endpoint_out_, const_cast<std::uint8_t*>(data.data()),
                       static_cast<std::uint16_t>(data.size())))
   {
      usbh_edpt_release(device_address_, endpoint_out_);
      status_ = TransferStatus::Idle;
      return false;
   }
   writes_started_ = writes_started_ + 1U;
   return true;
}

bool UsbHostTransport::start_read(std::span<std::uint8_t> data)
{
   if (!configured_ || status_ != TransferStatus::Idle || data.empty() || data.size() > 0xFFFFU ||
       !usbh_edpt_claim(device_address_, endpoint_in_))
   {
      return false;
   }
   status_ = TransferStatus::Busy;
   active_endpoint_ = endpoint_in_;
   read_buffer_ = data.data();
   if (!usbh_edpt_xfer(device_address_, endpoint_in_, data.data(),
                       static_cast<std::uint16_t>(data.size())))
   {
      usbh_edpt_release(device_address_, endpoint_in_);
      status_ = TransferStatus::Idle;
      return false;
   }
   reads_started_ = reads_started_ + 1U;
   return true;
}

void UsbHostTransport::clear_transfer()
{
   transferred_ = 0;
   status_ = TransferStatus::Idle;
   active_endpoint_ = 0;
}

void UsbHostTransport::abort_transfer()
{
   if (configured_ && status_ == TransferStatus::Busy)
      tuh_edpt_abort_xfer(device_address_, active_endpoint_);
   clear_transfer();
}

} // namespace psu_usb_to_pmbus::rp2040

extern "C" usbh_class_driver_t const* usbh_app_driver_get_cb(std::uint8_t* driver_count)
{
   *driver_count = 1;
   return &psu_usb_to_pmbus::rp2040::kDriver;
}

extern "C" void tuh_mount_cb(std::uint8_t dev_addr)
{
   psu_usb_to_pmbus::rp2040::UsbHostTransport::instance().note_mount(dev_addr);
}

extern "C" void tuh_umount_cb(std::uint8_t dev_addr)
{
   psu_usb_to_pmbus::rp2040::UsbHostTransport::instance().note_unmount(dev_addr);
}
