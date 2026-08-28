#include "psu_usb_to_pmbus/psu/corsair_ax1600i/backend.hpp"

#include "psu_usb_to_pmbus/pmbus/linear.hpp"
#include "psu_usb_to_pmbus/psu/corsair_ax1600i/protocol.hpp"
#include "psu_usb_to_pmbus/telemetry/power.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace psu_usb_to_pmbus::ax1600i
{
namespace
{
constexpr std::array<std::uint8_t, 7> kInitialize{0x11, 0x02, 0x64, 0, 0, 0, 0};
constexpr std::array<ControlRequest, 3> kUsbXpressOpen{
   ControlRequest{0x40, 0x02, 0x0002, 0}, // CLEAR_TO_SEND / device open
   ControlRequest{0x40, 0x02, 0x0001, 0}, // flush USBXpress RX and TX state
   ControlRequest{0x40, 0x02, 0x0002, 0}, // reassert open after the flush
};
constexpr std::uint64_t kRetryDelayMs = 1000;
// The AX1600i USBXpress bridge becomes unreliable under a continuous stream
// of back-to-back register transactions. iCUE refreshes user-visible data at
// roughly human-monitoring cadence; leave an explicit quiet interval between
// complete Corsair operations instead of saturating the bulk endpoints.
constexpr std::uint64_t kInterOperationDelayMs = 100;
// The reference clients use a 2-5 ms selector settling delay and otherwise
// complete the three-message register handshake promptly. A longer handshake
// delay did not reduce PIO-host transfer loss in live testing; throttling is
// therefore applied between complete operations, not inside the handshake.
constexpr std::uint64_t kInterWriteDelayMs = 5;
constexpr std::uint64_t kOperationRecoveryDelayMs = 50;
constexpr std::uint64_t kContinuousFailureResetMs = 10000;
constexpr std::uint64_t kTransferTimeoutMs = 500;
constexpr std::uint8_t kEmptyReplyRetries = 3;
} // namespace

void Backend::publish(std::uint64_t now_ms)
{
   working_.updated_ms = now_ms;
   ++working_.generation;
   store_.publish(working_);
}

void Backend::reset_link(std::uint64_t now_ms)
{
   const bool physically_connected = transport_.connected();
   if (physically_connected)
   {
      ++working_.ax_full_link_resets;
   }
   transport_.abort_transfer();
   link_state_ = physically_connected ? LinkState::Backoff : LinkState::Detached;
   exchange_state_ = ExchangeState::Idle;
   if (operation_ == Operation::ReadRegister && destination_ >= Destination::BranchCurrent &&
       destination_ <= Destination::BranchOcp)
   {
      selected_branch_ = static_cast<std::uint8_t>((selected_branch_ + 1U) % 12U);
   }
   operation_ = Operation::None;
   control_stage_ = 0;
   program_step_ = 0;
   selected_rail_ = 0;
   retry_at_ms_ = now_ms + kRetryDelayMs;
   operation_failure_since_ms_ = 0;
   if (!physically_connected)
   {
      working_.connected = false;
      working_.core_valid = false;
      validity_mask_ = 0;
      publish(now_ms);
   }
}

void Backend::recover_operation(std::uint64_t now_ms)
{
   ++working_.ax_local_resyncs;
   const bool selector_write_failed = operation_ == Operation::WriteRegister;
   if (operation_failure_since_ms_ == 0U)
   {
      operation_failure_since_ms_ = now_ms;
   }
   else if (now_ms >= operation_failure_since_ms_ + kContinuousFailureResetMs)
   {
      // Only a genuinely continuous failure is allowed to disrupt the attached
      // USBXpress session. Isolated failed samples are common on this bridge and
      // must not create PMBus INPUT/OFF oscillation.
      reset_link(now_ms);
      return;
   }

   // The reference drivers report malformed acknowledgements but continue
   // using the already-open bridge. The USBXpress vendor initialization is a
   // per-attachment operation, not routine error recovery. Discard the current
   // operation without disrupting an otherwise healthy USB session. A failed
   // selector write is the exception: reselect rail zero before continuing so
   // software and PSU page state cannot diverge.
   transport_.abort_transfer();
   exchange_state_ = ExchangeState::Idle;
   operation_ = Operation::None;
   destination_ = Destination::None;
   operation_stage_ = 0;
   if (selector_write_failed)
   {
      program_step_ = 4;
      selected_rail_ = 0;
   }
   next_action_ms_ = now_ms + kOperationRecoveryDelayMs;
}

void Backend::complete_probe(std::uint64_t now_ms, bool available)
{
   working_.probe.address = probe_register_;
   working_.probe.size = static_cast<std::uint8_t>(std::min<std::size_t>(response_size_, 0xFFU));
   working_.probe.available = available && response_size_ >= 2U;
   working_.probe.raw = working_.probe.available
                           ? static_cast<std::uint16_t>(response_[0] | (response_[1] << 8U))
                           : 0U;
   working_.probe.complete = probe_register_ == 0xFFU;
   ++working_.probe.sequence;
   publish(now_ms);
   if (working_.probe.complete)
   {
      scan_complete_ = true;
   }
   else
   {
      ++probe_register_;
   }
}

bool Backend::begin_exchange(std::span<const std::uint8_t> payload)
{
   if (exchange_state_ != ExchangeState::Idle || payload.size() > 62U)
   {
      return false;
   }
   tx_size_ = encode(0, payload, tx_);
   if (tx_size_ == 0U)
   {
      return false;
   }
   encoded_rx_size_ = 0;
   response_size_ = 0;
   exchange_retry_count_ = 0;
   exchange_state_ = ExchangeState::StartWrite;
   exchange_not_before_ms_ = current_time_ms_ + kInterWriteDelayMs;
   exchange_deadline_ms_ = current_time_ms_ + kTransferTimeoutMs;
   return true;
}

bool Backend::retry_empty_exchange()
{
   if (response_size_ != 0U || exchange_retry_count_ >= kEmptyReplyRetries)
   {
      return false;
   }
   ++working_.ax_empty_reply_retries;
   ++exchange_retry_count_;
   encoded_rx_size_ = 0;
   response_size_ = 0;
   exchange_state_ = ExchangeState::StartWrite;
   exchange_not_before_ms_ = current_time_ms_ + kInterWriteDelayMs;
   exchange_deadline_ms_ = current_time_ms_ + kTransferTimeoutMs;
   return true;
}

void Backend::service_exchange()
{
   if (exchange_state_ != ExchangeState::Idle && exchange_state_ != ExchangeState::Complete &&
       exchange_state_ != ExchangeState::Failed && current_time_ms_ >= exchange_deadline_ms_)
   {
      exchange_state_ = ExchangeState::Failed;
      return;
   }
   switch (exchange_state_)
   {
   case ExchangeState::StartWrite:
      if (current_time_ms_ < exchange_not_before_ms_)
         break;
      if (transport_.start_write(std::span<const std::uint8_t>(tx_.data(), tx_size_)))
      {
         exchange_state_ = ExchangeState::WaitWrite;
      }
      break;
   case ExchangeState::WaitWrite:
      if (transport_.transfer_status() == TransferStatus::Failed)
      {
         transport_.clear_transfer();
         exchange_state_ = ExchangeState::Failed;
      }
      else if (transport_.transfer_status() == TransferStatus::Complete)
      {
         const bool complete = transport_.transferred() == tx_size_;
         transport_.clear_transfer();
         exchange_state_ = complete ? ExchangeState::StartRead : ExchangeState::Failed;
      }
      break;
   case ExchangeState::StartRead:
      if (encoded_rx_size_ >= encoded_rx_.size())
      {
         exchange_state_ = ExchangeState::Failed;
      }
      else if (transport_.start_read(std::span<std::uint8_t>(
                  encoded_rx_.data() + encoded_rx_size_, encoded_rx_.size() - encoded_rx_size_)))
      {
         exchange_state_ = ExchangeState::WaitRead;
      }
      break;
   case ExchangeState::WaitRead:
      if (transport_.transfer_status() == TransferStatus::Failed)
      {
         transport_.clear_transfer();
         exchange_state_ = ExchangeState::Failed;
      }
      else if (transport_.transfer_status() == TransferStatus::Complete)
      {
         const auto received = transport_.transferred();
         transport_.clear_transfer();
         if (received == 0U || received > encoded_rx_.size() - encoded_rx_size_)
         {
            exchange_state_ = ExchangeState::Failed;
            break;
         }
         encoded_rx_size_ += received;
         if (encoded_rx_[encoded_rx_size_ - 1U] == 0U)
         {
            const auto result = decode(
               std::span<const std::uint8_t>(encoded_rx_.data(), encoded_rx_size_), response_);
            response_size_ = result.size;
            exchange_state_ = result ? ExchangeState::Complete : ExchangeState::Failed;
         }
         else
         {
            exchange_state_ = ExchangeState::StartRead;
         }
      }
      break;
   default:
      break;
   }
}

void Backend::finish_exchange()
{
   exchange_state_ = ExchangeState::Idle;
}

bool Backend::start_read_register(std::uint8_t reg, std::uint8_t length, Destination destination)
{
   const std::array<std::uint8_t, 7> header{0x13, 0x03, 0x06, 0x01, 0x07, length, reg};
   if (!begin_exchange(header))
   {
      return false;
   }
   operation_ = Operation::ReadRegister;
   operation_stage_ = 0;
   register_length_ = length;
   destination_ = destination;
   return true;
}

bool Backend::start_write_register(std::uint8_t reg, std::uint8_t value)
{
   const std::array<std::uint8_t, 6> request{0x13, 0x01, 0x04, 0x01, reg, value};
   if (!begin_exchange(request))
   {
      return false;
   }
   operation_ = Operation::WriteRegister;
   operation_stage_ = 0;
   destination_ = Destination::None;
   return true;
}

bool Backend::complete_read(std::uint64_t now_ms)
{
   if (destination_ == Destination::Probe)
   {
      complete_probe(now_ms, true);
      operation_failure_since_ms_ = 0;
      return true;
   }
   if (response_size_ < register_length_)
   {
      // The USB-to-PSU bridge occasionally emits an empty record. Match the
      // reference tools by keeping the link alive and leaving the previous
      // value untouched; freshness handling will invalidate stale telemetry.
      if (destination_ == Destination::BranchOcp)
         selected_branch_ = static_cast<std::uint8_t>((selected_branch_ + 1U) % 12U);
      return false;
   }
   if (destination_ == Destination::Model)
   {
      working_.model.fill('\0');
      const auto length = std::min<std::size_t>(register_length_, working_.model.size() - 1U);
      std::memcpy(working_.model.data(), response_.data(), length);
      model_read_ = true;
   }
   else if (register_length_ >= 2U)
   {
      const auto raw = static_cast<std::uint16_t>(response_[0] | (response_[1] << 8U));
      const auto value = pmbus::decode_linear11(raw);
      switch (destination_)
      {
      case Destination::Vin:
         working_.input_voltage = value;
         validity_mask_ |= 0x01;
         break;
      case Destination::Iin:
         working_.input_current = value;
         validity_mask_ |= 0x02;
         break;
      case Destination::Pin:
         working_.input_power = value;
         validity_mask_ |= 0x04;
         accumulate_input_power_sample(working_, value);
         break;
      case Destination::NativeTotalOutputPower:
         working_.native_total_output_power = value;
         break;
      case Destination::RailVoltage:
         working_.rails[selected_rail_].voltage = value;
         break;
      case Destination::RailCurrent:
         working_.rails[selected_rail_].current = value;
         break;
      case Destination::NativeRailPower:
      {
         auto& rail = working_.rails[selected_rail_];
         rail.native_power = value;
         rail.valid = true;
         update_derived_output_power(working_);
         if (selected_rail_ == 0U)
            validity_mask_ |= 0x08;
         break;
      }
      case Destination::Temperature1:
         working_.temperature_1 = value;
         break;
      case Destination::Temperature2:
         working_.temperature_2 = value;
         break;
      case Destination::Fan:
         working_.fan_rpm = value;
         break;
      case Destination::Uptime:
         working_.uptime_seconds = raw;
         break;
      case Destination::BranchCurrent:
         working_.branches[selected_branch_].current = value;
         working_.branches[selected_branch_].voltage = working_.rails[0].voltage;
         break;
      case Destination::BranchPower:
         working_.branches[selected_branch_].power = value;
         working_.branches[selected_branch_].valid = true;
         break;
      case Destination::BranchOcp:
         working_.branches[selected_branch_].ocp_limit = value;
         working_.branches[selected_branch_].valid = true;
         selected_branch_ = static_cast<std::uint8_t>((selected_branch_ + 1U) % 12U);
         break;
      default:
         break;
      }
   }
   working_.core_valid = (validity_mask_ & 0x0FU) == 0x0FU;
   if (working_.core_valid)
   {
      working_.core_updated_ms = now_ms;
   }
   operation_failure_since_ms_ = 0;
   publish(now_ms);
   return true;
}

void Backend::service_operation(std::uint64_t now_ms)
{
   if (operation_ == Operation::None)
   {
      return;
   }
   service_exchange();
   if (exchange_failed())
   {
      ++working_.ax_exchange_failures;
      recover_operation(now_ms);
      return;
   }
   if (!exchange_succeeded())
   {
      return;
   }

   if (operation_ == Operation::ReadRegister)
   {
      if (operation_stage_ == 0U)
      {
         if (!is_zero_reply(std::span<const std::uint8_t>(response_.data(), response_size_)))
         {
            if (retry_empty_exchange())
            {
               return;
            }
            ++working_.ax_header_ack_mismatches;
            recover_operation(now_ms);
            return;
         }
         finish_exchange();
         const std::array<std::uint8_t, 1> trigger{0x12};
         begin_exchange(trigger);
         operation_stage_ = 1;
      }
      else if (operation_stage_ == 1U)
      {
         if (!is_ok_reply(std::span<const std::uint8_t>(response_.data(), response_size_)))
         {
            if (retry_empty_exchange())
            {
               return;
            }
            ++working_.ax_trigger_ack_mismatches;
            if (destination_ == Destination::Probe)
            {
               complete_probe(now_ms, false);
            }
            recover_operation(now_ms);
            return;
         }
         finish_exchange();
         const std::array<std::uint8_t, 3> fetch{0x08, 0x07, register_length_};
         begin_exchange(fetch);
         operation_stage_ = 2;
      }
      else
      {
         if (response_size_ < register_length_ && retry_empty_exchange())
         {
            return;
         }
         if (!complete_read(now_ms))
         {
            ++working_.ax_exchange_failures;
            recover_operation(now_ms);
            return;
         }
         finish_exchange();
         operation_ = Operation::None;
         next_action_ms_ = now_ms + kInterOperationDelayMs;
      }
   }
   else
   {
      if (operation_stage_ == 0U)
      {
         if (!is_zero_reply(std::span<const std::uint8_t>(response_.data(), response_size_)))
         {
            if (retry_empty_exchange())
            {
               return;
            }
            ++working_.ax_header_ack_mismatches;
            recover_operation(now_ms);
            return;
         }
         finish_exchange();
         const std::array<std::uint8_t, 1> trigger{0x12};
         begin_exchange(trigger);
         operation_stage_ = 1;
      }
      else
      {
         if (!is_ok_reply(std::span<const std::uint8_t>(response_.data(), response_size_)))
         {
            if (retry_empty_exchange())
            {
               return;
            }
            ++working_.ax_trigger_ack_mismatches;
            recover_operation(now_ms);
            return;
         }
         finish_exchange();
         operation_ = Operation::None;
         operation_failure_since_ms_ = 0;
         next_action_ms_ = now_ms + kInterOperationDelayMs;
      }
   }
}

void Backend::schedule_next(std::uint64_t now_ms)
{
   if (operation_ != Operation::None || now_ms < next_action_ms_)
   {
      return;
   }
   if (!model_read_)
   {
      start_read_register(0x9A, 7, Destination::Model);
      return;
   }
   if (scan_registers_)
   {
      if (!scan_complete_)
      {
         start_read_register(probe_register_, 2, Destination::Probe);
      }
      return;
   }

   // Primary input telemetry is deliberately interleaved through the slower
   // rail/temperature/branch walk. At the measured ~100 ms operation cadence,
   // this refreshes native PIN close to iCUE's observed 1.5-second interval
   // without returning to an uninterrupted USB request stream.
   switch (program_step_)
   {
   case 0:
      start_read_register(0x88, 2, Destination::Vin);
      break;
   case 1:
      start_read_register(0x89, 2, Destination::Iin);
      break;
   case 2:
      start_read_register(0x97, 2, Destination::Pin);
      break;
   case 3:
      start_read_register(0xEE, 2, Destination::NativeTotalOutputPower);
      break;
   case 4:
      selected_rail_ = 0;
      start_write_register(0x00, selected_rail_);
      break;
   case 5:
      start_read_register(0x8B, 2, Destination::RailVoltage);
      break;
   case 6:
      start_read_register(0x8C, 2, Destination::RailCurrent);
      break;
   case 7:
      start_read_register(0x96, 2, Destination::NativeRailPower);
      break;
   case 8:
      start_read_register(0x88, 2, Destination::Vin);
      break;
   case 9:
      start_read_register(0x89, 2, Destination::Iin);
      break;
   case 10:
      start_read_register(0x97, 2, Destination::Pin);
      break;
   case 11:
      start_read_register(0x8E, 2, Destination::Temperature1);
      break;
   case 12:
      start_read_register(0x8D, 2, Destination::Temperature2);
      break;
   case 13:
      start_read_register(0x90, 2, Destination::Fan);
      break;
   case 14:
      start_read_register(0xD2, 2, Destination::Uptime);
      break;
   case 15:
      selected_rail_ = 1;
      start_write_register(0x00, selected_rail_);
      break;
   case 16:
      start_read_register(0x8B, 2, Destination::RailVoltage);
      break;
   case 17:
      start_read_register(0x8C, 2, Destination::RailCurrent);
      break;
   case 18:
      start_read_register(0x96, 2, Destination::NativeRailPower);
      break;
   case 19:
      start_read_register(0x88, 2, Destination::Vin);
      break;
   case 20:
      start_read_register(0x89, 2, Destination::Iin);
      break;
   case 21:
      start_read_register(0x97, 2, Destination::Pin);
      break;
   case 22:
      selected_rail_ = 2;
      start_write_register(0x00, selected_rail_);
      break;
   case 23:
      start_read_register(0x8B, 2, Destination::RailVoltage);
      break;
   case 24:
      start_read_register(0x8C, 2, Destination::RailCurrent);
      break;
   case 25:
      start_read_register(0x96, 2, Destination::NativeRailPower);
      break;
   case 26:
      selected_rail_ = 0;
      start_write_register(0x00, selected_rail_);
      break;
   case 27:
      start_write_register(0xE7, selected_branch_);
      break;
   case 28:
      start_read_register(0xE8, 2, Destination::BranchCurrent);
      break;
   case 29:
      start_read_register(0xE9, 2, Destination::BranchPower);
      break;
   case 30:
      start_read_register(0xEA, 2, Destination::BranchOcp);
      break;
   default:
      break;
   }
   program_step_ = static_cast<std::uint8_t>((program_step_ + 1U) % 31U);
}

void Backend::task(std::uint64_t now_ms)
{
   current_time_ms_ = now_ms;
   if (!transport_.connected())
   {
      if (link_state_ != LinkState::Detached || working_.connected)
      {
         reset_link(now_ms);
         link_state_ = LinkState::Detached;
      }
      return;
   }

   if (link_state_ == LinkState::Detached)
   {
      link_state_ = LinkState::StartControl;
   }
   else if (link_state_ == LinkState::Backoff && now_ms >= retry_at_ms_)
   {
      link_state_ = LinkState::StartControl;
   }

   switch (link_state_)
   {
   case LinkState::StartControl:
   {
      if (transport_.start_control(kUsbXpressOpen[control_stage_]))
      {
         link_state_ = LinkState::WaitControl;
         control_deadline_ms_ = now_ms + kTransferTimeoutMs;
      }
      break;
   }
   case LinkState::WaitControl:
      if (transport_.transfer_status() == TransferStatus::Failed)
      {
         reset_link(now_ms);
      }
      else if (transport_.transfer_status() == TransferStatus::Complete)
      {
         transport_.clear_transfer();
         ++control_stage_;
         link_state_ = control_stage_ < kUsbXpressOpen.size() ? LinkState::StartControl
                                                              : LinkState::StartInitialize;
      }
      else if (now_ms >= control_deadline_ms_)
      {
         reset_link(now_ms);
      }
      break;
   case LinkState::StartInitialize:
      if (begin_exchange(kInitialize))
      {
         link_state_ = LinkState::WaitInitialize;
      }
      break;
   case LinkState::WaitInitialize:
      service_exchange();
      if (exchange_failed())
      {
         reset_link(now_ms);
      }
      else if (exchange_succeeded())
      {
         if (!is_zero_reply(std::span<const std::uint8_t>(response_.data(), response_size_)))
         {
            if (retry_empty_exchange())
            {
               break;
            }
            reset_link(now_ms);
            break;
         }
         finish_exchange();
         if (!working_.connected)
         {
            working_.connected = true;
            publish(now_ms);
         }
         link_state_ = LinkState::Ready;
         next_action_ms_ = now_ms;
      }
      break;
   case LinkState::Ready:
      service_operation(now_ms);
      if (link_state_ == LinkState::Ready)
      {
         schedule_next(now_ms);
      }
      break;
   default:
      break;
   }
}

} // namespace psu_usb_to_pmbus::ax1600i
