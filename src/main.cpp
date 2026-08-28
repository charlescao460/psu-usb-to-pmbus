#include "corsair_usb2pmbus/pmbus/linear.hpp"
#include "corsair_usb2pmbus/pmbus/server.hpp"
#include "corsair_usb2pmbus/psu/corsair_ax1600i/backend.hpp"
#include "corsair_usb2pmbus/telemetry/store.hpp"
#include "i2c_target.hpp"
#include "usb_transport.hpp"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "pio_usb.h"
#include "tusb.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
constexpr std::uint8_t kPioUsbRootPort = 1;
constexpr std::uint8_t kUsbDpPin = 16;
constexpr std::uint8_t kUsbVbusEnablePin = 18;

cusb2pmbus::TelemetryStore telemetry;

void usb_core()
{
   sleep_ms(10);
   pio_usb_configuration_t configuration = PIO_USB_DEFAULT_CONFIG;
   configuration.pin_dp = kUsbDpPin;
   const bool configured =
      tuh_configure(kPioUsbRootPort, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &configuration);
   const tusb_rhport_init_t host_init{.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO};
   const bool initialized = tusb_init(kPioUsbRootPort, &host_init);

   auto& transport = cusb2pmbus::rp2040::UsbHostTransport::instance();
   transport.note_host_init(configured, initialized);
   cusb2pmbus::ax1600i::Backend backend(transport, telemetry,
                                        CUSB2PMBUS_AX1600I_REGISTER_SCAN != 0);
   while (true)
   {
      tuh_task_ext(0, false);
      backend.task(to_ms_since_boot(get_absolute_time()));
      tight_loop_contents();
   }
}
} // namespace

int main()
{
   set_sys_clock_khz(120000, true);
   const tusb_rhport_init_t device_init{.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO};
   tusb_init(0, &device_init);
   stdio_init_all();

   char board_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2U + 1U]{};
   pico_get_unique_board_id_string(board_id, sizeof(board_id));
   auto initial_snapshot = telemetry.read();
   std::memcpy(initial_snapshot.serial.data(), board_id, sizeof(board_id));
   telemetry.publish(initial_snapshot);

   gpio_init(kUsbVbusEnablePin);
   gpio_set_dir(kUsbVbusEnablePin, GPIO_OUT);
   gpio_put(kUsbVbusEnablePin, 1);

   multicore_reset_core1();
   multicore_launch_core1(usb_core);

   cusb2pmbus::pmbus::PmbusServer server(telemetry, CUSB2PMBUS_PMBUS_ADDRESS);
   cusb2pmbus::rp2040::I2cTarget target(server);
   target.initialize();

   std::uint64_t last_log_ms = 0;
#if CUSB2PMBUS_AX1600I_REGISTER_SCAN
   std::uint32_t last_probe_sequence = 0;
#endif
   while (true)
   {
      const auto now_ms = to_ms_since_boot(get_absolute_time());
      tud_task_ext(0, false);
      target.task(now_ms);
#if CUSB2PMBUS_LOG_LEVEL > 0
      const auto current_snapshot = telemetry.read();
#if CUSB2PMBUS_AX1600I_REGISTER_SCAN
      if (current_snapshot.probe.sequence != last_probe_sequence)
      {
         std::printf("AXSCAN reg=%02x available=%u size=%u raw=%04x linear=%.6f complete=%u\n",
                     current_snapshot.probe.address, current_snapshot.probe.available,
                     current_snapshot.probe.size, current_snapshot.probe.raw,
                     cusb2pmbus::pmbus::decode_linear11(current_snapshot.probe.raw),
                     current_snapshot.probe.complete);
         last_probe_sequence = current_snapshot.probe.sequence;
      }
#endif
      if (now_ms - last_log_ms >= 1000U)
      {
         const auto snapshot = current_snapshot;
         const auto usb = cusb2pmbus::rp2040::UsbHostTransport::instance().diagnostics();
         std::printf("AX1600i connected=%u valid=%u age_ms=%llu vin=%.2f iin=%.2f pin=%.1f/%lu "
                     "pout=%.1f/native=%.1f 12v=%.2f/%.2f/%.1f/native=%.1f "
                     "temp=%.1f/%.1f fan=%.0f recover=%lu/%lu fail=%lu ack=%lu/%lu empty=%lu "
                     "host=%u/%u usb=%u %04x:%04x opens=%lu itf=%u/%02x reject=%u "
                     "cfg=%u/%lu xfer=%u c=%lu/%lu w=%lu r=%lu/%lu:%lu[%02x%02x%02x%02x] "
                     "ep=%02x result=%u\n",
                     snapshot.connected, snapshot.core_valid,
                     static_cast<unsigned long long>(
                        now_ms >= snapshot.core_updated_ms ? now_ms - snapshot.core_updated_ms : 0),
                     snapshot.input_voltage, snapshot.input_current, snapshot.input_power,
                     static_cast<unsigned long>(snapshot.input_power_sample_count),
                     snapshot.total_output_power, snapshot.native_total_output_power,
                     snapshot.rails[0].voltage, snapshot.rails[0].current, snapshot.rails[0].power,
                     snapshot.rails[0].native_power, snapshot.temperature_1, snapshot.temperature_2,
                     snapshot.fan_rpm, static_cast<unsigned long>(snapshot.ax_full_link_resets),
                     static_cast<unsigned long>(snapshot.ax_local_resyncs),
                     static_cast<unsigned long>(snapshot.ax_exchange_failures),
                     static_cast<unsigned long>(snapshot.ax_header_ack_mismatches),
                     static_cast<unsigned long>(snapshot.ax_trigger_ack_mismatches),
                     static_cast<unsigned long>(snapshot.ax_empty_reply_retries),
                     usb.host_configured, usb.host_initialized, usb.mounted_address, usb.vid,
                     usb.pid, static_cast<unsigned long>(usb.open_attempts), usb.interface_number,
                     usb.interface_class, usb.reject_code, usb.transport_configured,
                     static_cast<unsigned long>(usb.set_config_attempts),
                     static_cast<unsigned>(usb.transfer_status),
                     static_cast<unsigned long>(usb.controls_completed),
                     static_cast<unsigned long>(usb.controls_started),
                     static_cast<unsigned long>(usb.writes_started),
                     static_cast<unsigned long>(usb.reads_started),
                     static_cast<unsigned long>(usb.reads_completed),
                     static_cast<unsigned long>(usb.last_read_size), usb.last_read_bytes[0],
                     usb.last_read_bytes[1], usb.last_read_bytes[2], usb.last_read_bytes[3],
                     usb.last_endpoint, usb.last_result);
         last_log_ms = now_ms;
      }
#endif
      sleep_ms(1);
   }
}
