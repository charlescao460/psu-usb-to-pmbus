#pragma once

#include <cstdint>

namespace cusb2pmbus::pmbus
{

enum class Command : std::uint8_t
{
   Page = 0x00,
   ClearFaults = 0x03,
   PagePlusWrite = 0x05,
   PagePlusRead = 0x06,
   Capability = 0x19,
   Query = 0x1A,
   SmAlertMask = 0x1B,
   VoutMode = 0x20,
   Coefficients = 0x30,
   FanConfig12 = 0x3A,
   FanCommand1 = 0x3B,
   StatusByte = 0x78,
   StatusWord = 0x79,
   StatusIout = 0x7B,
   StatusInput = 0x7C,
   StatusTemperature = 0x7D,
   StatusCml = 0x7E,
   StatusFans12 = 0x81,
   ReadEin = 0x86,
   ReadVin = 0x88,
   ReadIin = 0x89,
   ReadVout = 0x8B,
   ReadIout = 0x8C,
   ReadTemperature1 = 0x8D,
   ReadTemperature2 = 0x8E,
   ReadFanSpeed1 = 0x90,
   ReadPout = 0x96,
   ReadPin = 0x97,
   PmbusRevision = 0x98,
   MfrId = 0x99,
   MfrModel = 0x9A,
   MfrRevision = 0x9B,
   MfrSerial = 0x9E,
   AppProfileSupport = 0x9F,
   MfrIoutMax = 0xA6,
   MfrPoutMax = 0xA7,
   MfrBridgeStatus = 0xD0,
   MfrUptime = 0xD2,
   MfrBranchOcp = 0xEA,
   MfrMaxTemp1 = 0xC0,
   MfrMaxTemp2 = 0xC1,
};

constexpr bool is_valid_page(std::uint8_t page)
{
   return page <= 0x02U || (page >= 0x10U && page <= 0x1BU);
}

} // namespace cusb2pmbus::pmbus
