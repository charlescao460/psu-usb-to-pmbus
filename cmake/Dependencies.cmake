include(FetchContent)

set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(
  tinyusb
  GIT_REPOSITORY https://github.com/hathach/tinyusb.git
  GIT_TAG 0.21.0
  GIT_SHALLOW TRUE
  GIT_SUBMODULES ""
)
FetchContent_Declare(
  pico_pio_usb
  GIT_REPOSITORY https://github.com/sekigon-gonnoc/Pico-PIO-USB.git
  GIT_TAG 675543bcc9baa8170f868ab7ba316d418dbcf41f
)

FetchContent_GetProperties(tinyusb)
if(NOT tinyusb_POPULATED)
  FetchContent_Populate(tinyusb)
endif()
FetchContent_GetProperties(pico_pio_usb)
if(NOT pico_pio_usb_POPULATED)
  FetchContent_Populate(pico_pio_usb)
endif()

# ATfE Clang otherwise emits a call in Pico-PIO-USB's timing-critical CRC
# loop, delaying full-speed ACKs long enough for the AX1600i to miss them.
set(_pio_usb_clang_patch
    "${CMAKE_CURRENT_LIST_DIR}/patches/pico-pio-usb-clang-crc-inline.patch")
execute_process(
  COMMAND git apply --check "${_pio_usb_clang_patch}"
  WORKING_DIRECTORY "${pico_pio_usb_SOURCE_DIR}"
  RESULT_VARIABLE _pio_usb_patch_check
  OUTPUT_QUIET ERROR_QUIET
)
if(_pio_usb_patch_check EQUAL 0)
  execute_process(
    COMMAND git apply "${_pio_usb_clang_patch}"
    WORKING_DIRECTORY "${pico_pio_usb_SOURCE_DIR}"
    RESULT_VARIABLE _pio_usb_patch_result
  )
  if(NOT _pio_usb_patch_result EQUAL 0)
    message(FATAL_ERROR "Failed to apply the Pico-PIO-USB Clang timing patch")
  endif()
else()
  execute_process(
    COMMAND git apply --reverse --check "${_pio_usb_clang_patch}"
    WORKING_DIRECTORY "${pico_pio_usb_SOURCE_DIR}"
    RESULT_VARIABLE _pio_usb_patch_already_applied
    OUTPUT_QUIET ERROR_QUIET
  )
  if(NOT _pio_usb_patch_already_applied EQUAL 0)
    message(FATAL_ERROR "Pico-PIO-USB source does not match the pinned patch revision")
  endif()
endif()
unset(_pio_usb_clang_patch)
unset(_pio_usb_patch_check)
unset(_pio_usb_patch_result)
unset(_pio_usb_patch_already_applied)

set(PICO_TINYUSB_PATH "${tinyusb_SOURCE_DIR}" CACHE PATH "TinyUSB source" FORCE)
set(PICO_PIO_USB_PATH "${pico_pio_usb_SOURCE_DIR}" CACHE PATH "Pico-PIO-USB source" FORCE)
