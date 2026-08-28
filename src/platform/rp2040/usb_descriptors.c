#include "tusb.h"

#include <string.h>

enum
{
   ITF_NUM_CDC = 0,
   ITF_NUM_CDC_DATA,
   ITF_NUM_TOTAL,
};

static const tusb_desc_device_t device_descriptor = {
   .bLength = sizeof(tusb_desc_device_t),
   .bDescriptorType = TUSB_DESC_DEVICE,
   .bcdUSB = 0x0200,
   .bDeviceClass = TUSB_CLASS_MISC,
   .bDeviceSubClass = MISC_SUBCLASS_COMMON,
   .bDeviceProtocol = MISC_PROTOCOL_IAD,
   .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
   .idVendor = 0x239A,
   .idProduct = 0xCAFE,
   .bcdDevice = 0x0100,
   .iManufacturer = 1,
   .iProduct = 2,
   .iSerialNumber = 3,
   .bNumConfigurations = 1,
};

const uint8_t* tud_descriptor_device_cb(void)
{
   return (const uint8_t*)&device_descriptor;
}

#define CONFIG_TOTAL_LENGTH (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t configuration_descriptor[] = {
   TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LENGTH, 0x00, 100),
   TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, 0x81, 16, 0x02, 0x82, 64),
};

const uint8_t* tud_descriptor_configuration_cb(uint8_t index)
{
   (void)index;
   return configuration_descriptor;
}

static const char* string_descriptors[] = {
   (const char[]){0x09, 0x04}, "psu-usb-to-pmbus", "psu-usb-to-pmbus Debug",
   "RP2040-AX1600I",           "Debug CDC",
};
static uint16_t string_buffer[33];

const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t language_id)
{
   (void)language_id;
   uint8_t length = 0;
   if (index == 0)
   {
      memcpy(&string_buffer[1], string_descriptors[0], 2);
      length = 1;
   }
   else
   {
      if (index >= sizeof(string_descriptors) / sizeof(string_descriptors[0]))
         return NULL;
      length = (uint8_t)strlen(string_descriptors[index]);
      if (length > 32)
         length = 32;
      for (uint8_t i = 0; i < length; ++i)
         string_buffer[1 + i] = string_descriptors[index][i];
   }
   string_buffer[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * length + 2));
   return string_buffer;
}
