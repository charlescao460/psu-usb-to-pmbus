#pragma once

#include "psu_usb_to_pmbus/platform/usb_transport.hpp"
#include "psu_usb_to_pmbus/psu/backend.hpp"
#include "psu_usb_to_pmbus/telemetry/store.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace psu_usb_to_pmbus::ax1600i
{

class Backend final : public PsuBackend
{
public:
   Backend(UsbTransport& transport, TelemetryStore& store, bool scan_registers = false)
      : transport_(transport), store_(store), working_(store.read()),
        scan_registers_(scan_registers)
   {
      working_.rated_output_power = 1600.0F;
   }

   void task(std::uint64_t now_ms) override;
   bool ready() const override
   {
      return link_state_ == LinkState::Ready;
   }

private:
   enum class LinkState : std::uint8_t
   {
      Detached,
      StartControl,
      WaitControl,
      StartInitialize,
      WaitInitialize,
      Ready,
      Backoff,
   };
   enum class ExchangeState : std::uint8_t
   {
      Idle,
      StartWrite,
      WaitWrite,
      StartRead,
      WaitRead,
      Complete,
      Failed,
   };
   enum class Operation : std::uint8_t
   {
      None,
      ReadRegister,
      WriteRegister
   };
   enum class Destination : std::uint8_t
   {
      None,
      Model,
      Vin,
      Iin,
      Pin,
      NativeTotalOutputPower,
      RailVoltage,
      RailCurrent,
      NativeRailPower,
      Temperature1,
      Temperature2,
      Fan,
      Uptime,
      BranchCurrent,
      BranchPower,
      BranchOcp,
      Probe,
   };

   void reset_link(std::uint64_t now_ms);
   bool begin_exchange(std::span<const std::uint8_t> payload);
   bool retry_empty_exchange();
   void service_exchange();
   bool exchange_succeeded() const
   {
      return exchange_state_ == ExchangeState::Complete;
   }
   bool exchange_failed() const
   {
      return exchange_state_ == ExchangeState::Failed;
   }
   void finish_exchange();

   bool start_read_register(std::uint8_t reg, std::uint8_t length, Destination destination);
   bool start_write_register(std::uint8_t reg, std::uint8_t value);
   void service_operation(std::uint64_t now_ms);
   bool complete_read(std::uint64_t now_ms);
   void schedule_next(std::uint64_t now_ms);
   void publish(std::uint64_t now_ms);
   void complete_probe(std::uint64_t now_ms, bool available);
   void recover_operation(std::uint64_t now_ms);

   UsbTransport& transport_;
   TelemetryStore& store_;
   TelemetrySnapshot working_{};
   LinkState link_state_{LinkState::Detached};
   ExchangeState exchange_state_{ExchangeState::Idle};
   Operation operation_{Operation::None};
   Destination destination_{Destination::None};
   std::array<std::uint8_t, 128> tx_{};
   std::array<std::uint8_t, 256> encoded_rx_{};
   std::array<std::uint8_t, 64> response_{};
   std::size_t tx_size_{};
   std::size_t encoded_rx_size_{};
   std::size_t response_size_{};
   std::uint8_t operation_stage_{};
   std::uint8_t control_stage_{};
   std::uint8_t exchange_retry_count_{};
   std::uint8_t register_length_{};
   std::uint8_t selected_rail_{};
   std::uint8_t selected_branch_{};
   std::uint8_t program_step_{};
   std::uint8_t validity_mask_{};
   std::uint64_t retry_at_ms_{};
   std::uint64_t next_action_ms_{};
   std::uint64_t current_time_ms_{};
   std::uint64_t exchange_not_before_ms_{};
   std::uint64_t exchange_deadline_ms_{};
   std::uint64_t control_deadline_ms_{};
   std::uint64_t operation_failure_since_ms_{};
   bool model_read_{};
   bool scan_registers_{};
   bool scan_complete_{};
   std::uint8_t probe_register_{};
};

} // namespace psu_usb_to_pmbus::ax1600i
