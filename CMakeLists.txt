cmake_minimum_required(VERSION 3.25)

option(PSU_USB_TO_PMBUS_BUILD_FIRMWARE "Build the RP2040 firmware" ON)
include(cmake/SelectTarget.cmake)

if(PSU_USB_TO_PMBUS_BUILD_FIRMWARE)
  include(cmake/pico_sdk_import.cmake)
endif()

project(psu-usb-to-pmbus VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_library(psu_usb_to_pmbus_core STATIC
  src/psu/corsair_ax1600i/protocol.cpp
  src/psu/corsair_ax1600i/backend.cpp
  src/pmbus/linear.cpp
  src/pmbus/pec.cpp
  src/pmbus/server.cpp
  src/telemetry/power.cpp
  src/telemetry/store.cpp
)
target_include_directories(psu_usb_to_pmbus_core PUBLIC include)
target_compile_options(psu_usb_to_pmbus_core PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>
)

if(PSU_USB_TO_PMBUS_BUILD_FIRMWARE)
  enable_language(ASM)
  include(cmake/Dependencies.cmake)
  pico_sdk_init()

  add_executable(psu_usb_to_pmbus
    src/main.cpp
    src/platform/rp2040/usb_transport.cpp
    src/platform/rp2040/i2c_target.cpp
    src/platform/rp2040/usb_descriptors.c
  )
  set_target_properties(psu_usb_to_pmbus PROPERTIES OUTPUT_NAME "psu-usb-to-pmbus")
  target_include_directories(psu_usb_to_pmbus PRIVATE
    include
    src/platform/rp2040
    "${tinyusb_SOURCE_DIR}/src"
  )
  target_compile_definitions(psu_usb_to_pmbus PRIVATE
    PSU_USB_TO_PMBUS_PMBUS_ADDRESS=${PSU_USB_TO_PMBUS_PMBUS_ADDRESS}
    PSU_USB_TO_PMBUS_LOG_LEVEL=${PSU_USB_TO_PMBUS_LOG_LEVEL}
    PSU_USB_TO_PMBUS_AX1600I_REGISTER_SCAN=$<BOOL:${PSU_USB_TO_PMBUS_AX1600I_REGISTER_SCAN}>
    CFG_TUH_RPI_PIO_USB=1
    PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=1
    PIO_USB_USE_TINYUSB=1
  )
  target_compile_options(psu_usb_to_pmbus PRIVATE
    -Wall -Wextra -fno-exceptions -fno-rtti -fno-threadsafe-statics
  )
  target_link_options(psu_usb_to_pmbus PRIVATE -fno-exceptions -fno-rtti)
  target_link_libraries(psu_usb_to_pmbus PRIVATE
    psu_usb_to_pmbus_core
    pico_stdlib
    pico_unique_id
    pico_multicore
    pico_i2c_slave
    hardware_i2c
    hardware_pio
    tinyusb_host
    tinyusb_pico_pio_usb
  )
  pico_enable_stdio_usb(psu_usb_to_pmbus 1)
  pico_enable_stdio_uart(psu_usb_to_pmbus 0)
  pico_add_extra_outputs(psu_usb_to_pmbus)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  add_executable(core_tests tests/cpp/core_tests.cpp)
  target_link_libraries(core_tests PRIVATE psu_usb_to_pmbus_core)
  add_test(NAME core_tests COMMAND core_tests)
endif()
