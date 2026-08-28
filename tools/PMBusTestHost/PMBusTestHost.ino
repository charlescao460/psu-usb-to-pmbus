#include <Wire.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef SERIAL_PORT_USBVIRTUAL
#define HOST_SERIAL SERIAL_PORT_USBVIRTUAL
#else
#define HOST_SERIAL Serial
#endif

namespace
{
constexpr unsigned long kBaud = 115200;
constexpr size_t kLineCapacity = 160;
constexpr size_t kI2cCapacity = 32;
char line[kLineCapacity];
size_t lineLength = 0;

void error(const char* sequence, const char* code, const char* detail)
{
   HOST_SERIAL.print(F("ERR "));
   HOST_SERIAL.print(sequence == nullptr ? "0" : sequence);
   HOST_SERIAL.print(' ');
   HOST_SERIAL.print(code);
   HOST_SERIAL.print(' ');
   HOST_SERIAL.println(detail);
}

bool parseUnsigned(const char* text, unsigned long& value)
{
   if (text == nullptr || *text == '\0')
      return false;
   char* end = nullptr;
   value = strtoul(text, &end, 0);
   return end != text && *end == '\0';
}

bool parseHex(const char* text, uint8_t* output, size_t& outputLength)
{
   outputLength = 0;
   if (text == nullptr || strcmp(text, "-") == 0)
      return true;
   const size_t length = strlen(text);
   if ((length & 1U) != 0U || length / 2U > kI2cCapacity)
      return false;
   for (size_t i = 0; i < length; i += 2)
   {
      const char pair[3] = {text[i], text[i + 1], '\0'};
      char* end = nullptr;
      const unsigned long value = strtoul(pair, &end, 16);
      if (end != pair + 2 || value > 0xFFUL)
         return false;
      output[outputLength++] = static_cast<uint8_t>(value);
   }
   return true;
}

const char* wireError(uint8_t status)
{
   switch (status)
   {
   case 2:
      return "NACK_ADDR";
   case 3:
      return "NACK_DATA";
   default:
      return "I2C";
   }
}

void information(const char* sequence)
{
   HOST_SERIAL.print(F("OK "));
   HOST_SERIAL.print(sequence);
   HOST_SERIAL.println(F(" PMBusTestHost/1 MAXW=32 MAXR=32 RS=1 I2C_HZ=100000"));
}

void transaction(char* sequence, char* addressText, char* mode, char* readText, char* writeText)
{
   unsigned long address = 0;
   unsigned long readLengthValue = 0;
   if (!parseUnsigned(addressText, address) || address > 0x7FUL ||
       !parseUnsigned(readText, readLengthValue) || readLengthValue > kI2cCapacity ||
       (strcmp(mode, "RS") != 0 && strcmp(mode, "STOP") != 0))
   {
      error(sequence, "RANGE", "address/mode/length");
      return;
   }

   uint8_t writeData[kI2cCapacity];
   size_t writeLength = 0;
   if (!parseHex(writeText, writeData, writeLength))
   {
      error(sequence, "PARSE", "write_hex");
      return;
   }

   if (writeLength != 0U)
   {
      Wire.beginTransmission(static_cast<uint8_t>(address));
      if (Wire.write(writeData, writeLength) != writeLength)
      {
         error(sequence, "I2C", "tx_buffer");
         return;
      }
      const bool sendStop = readLengthValue == 0U || strcmp(mode, "STOP") == 0;
      const uint8_t status = Wire.endTransmission(sendStop);
      if (status != 0U)
      {
         error(sequence, wireError(status), "endTransmission");
         return;
      }
   }

   uint8_t readData[kI2cCapacity];
   size_t received = 0;
   if (readLengthValue != 0U)
   {
      received =
         Wire.requestFrom(static_cast<uint8_t>(address), static_cast<uint8_t>(readLengthValue),
                          static_cast<uint8_t>(true));
      while (Wire.available() && received <= kI2cCapacity)
      {
         const int value = Wire.read();
         const size_t stored =
            static_cast<size_t>(readLengthValue) - static_cast<size_t>(Wire.available()) - 1U;
         if (stored < kI2cCapacity && value >= 0)
            readData[stored] = static_cast<uint8_t>(value);
      }
      if (received != readLengthValue)
      {
         char detail[20];
         snprintf(detail, sizeof(detail), "%u/%lu", static_cast<unsigned>(received),
                  readLengthValue);
         error(sequence, "SHORT_READ", detail);
         return;
      }
   }

   HOST_SERIAL.print(F("OK "));
   HOST_SERIAL.print(sequence);
   HOST_SERIAL.print(' ');
   if (readLengthValue == 0U)
   {
      HOST_SERIAL.println('-');
      return;
   }
   constexpr char hex[] = "0123456789ABCDEF";
   for (size_t i = 0; i < static_cast<size_t>(readLengthValue); ++i)
   {
      HOST_SERIAL.print(hex[readData[i] >> 4U]);
      HOST_SERIAL.print(hex[readData[i] & 0x0FU]);
   }
   HOST_SERIAL.println();
}

void processLine(char* input)
{
   char* save = nullptr;
   char* command = strtok_r(input, " ", &save);
   char* sequence = strtok_r(nullptr, " ", &save);
   if (command == nullptr || sequence == nullptr)
   {
      error(sequence, "PARSE", "command");
      return;
   }
   if (strcmp(command, "I") == 0)
   {
      if (strtok_r(nullptr, " ", &save) != nullptr)
         error(sequence, "PARSE", "extra");
      else
         information(sequence);
      return;
   }
   if (strcmp(command, "X") != 0)
   {
      error(sequence, "PARSE", "unknown_command");
      return;
   }
   char* address = strtok_r(nullptr, " ", &save);
   char* mode = strtok_r(nullptr, " ", &save);
   char* readLength = strtok_r(nullptr, " ", &save);
   char* writeHex = strtok_r(nullptr, " ", &save);
   if (address == nullptr || mode == nullptr || readLength == nullptr || writeHex == nullptr ||
       strtok_r(nullptr, " ", &save) != nullptr)
   {
      error(sequence, "PARSE", "arguments");
      return;
   }
   transaction(sequence, address, mode, readLength, writeHex);
}
} // namespace

void setup()
{
   HOST_SERIAL.begin(kBaud);
   Wire.begin();
   Wire.setClock(100000UL);
}

void loop()
{
   while (HOST_SERIAL.available())
   {
      const int value = HOST_SERIAL.read();
      if (value < 0)
         return;
      const char c = static_cast<char>(value);
      if (c == '\r')
         continue;
      if (c == '\n')
      {
         line[lineLength] = '\0';
         if (lineLength != 0U)
            processLine(line);
         lineLength = 0;
      }
      else if (lineLength + 1U < kLineCapacity)
      {
         line[lineLength++] = c;
      }
      else
      {
         lineLength = 0;
         error("0", "RANGE", "line_too_long");
      }
   }
}
