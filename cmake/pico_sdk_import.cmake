# Pico SDK's supported import approach, with an ignored in-tree default.
if(NOT PICO_SDK_PATH)
  if(DEFINED ENV{PICO_SDK_PATH} AND NOT "$ENV{PICO_SDK_PATH}" STREQUAL "")
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
  else()
    get_filename_component(_project_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    set(PICO_SDK_PATH "${_project_root}/toolchain/pico-sdk")
  endif()
endif()

get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" ABSOLUTE)
if(NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
  message(FATAL_ERROR
    "Pico SDK 2.3.0 was not found at ${PICO_SDK_PATH}. "
    "Run scripts/bootstrap-toolchain.ps1 (or .sh), or set PICO_SDK_PATH.")
endif()
include("${PICO_SDK_PATH}/pico_sdk_init.cmake")

