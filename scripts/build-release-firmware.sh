#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
release_root="$project_root/build/release"

export PICO_SDK_PATH="$project_root/toolchain/pico-sdk"
export PICO_TOOLCHAIN_PATH="$project_root/toolchain/arm"
pioasm_dir="$project_root/toolchain/pico-tools/pioasm"
picotool_dir="$project_root/toolchain/pico-tools/picotool"

if [[ ! -x "$PICO_TOOLCHAIN_PATH/bin/clang" ||
      ! -f "$PICO_SDK_PATH/pico_sdk_init.cmake" ||
      ! -x "$pioasm_dir/pioasm" ||
      ! -x "$picotool_dir/picotool" ]]; then
  echo 'Pinned toolchain is incomplete; run scripts/bootstrap-toolchain.sh first.' >&2
  exit 1
fi

mkdir -p "$release_root"

build_firmware()
{
   local address="$1"
   local build_dir="$project_root/build/rp2040-ax1600i-pmbus-$address"
   local asset_name="psu-usb-to-pmbus-rp2040-ax1600i-pmbus-$address.uf2"

   cmake --preset rp2040-ax1600i \
      -B "$build_dir" \
      "-DPSU_USB_TO_PMBUS_PMBUS_ADDRESS=$address" \
      "-Dpioasm_DIR=$pioasm_dir" \
      "-Dpicotool_DIR=$picotool_dir"
   cmake --build "$build_dir" --target psu_usb_to_pmbus --parallel

   if ! grep -Fq -- "-DPSU_USB_TO_PMBUS_PMBUS_ADDRESS=$address" \
      "$build_dir/compile_commands.json"; then
      echo "Build did not use requested PMBus address $address" >&2
      exit 1
   fi
   if [[ ! -s "$build_dir/psu-usb-to-pmbus.uf2" ]]; then
      echo "UF2 output is missing for PMBus address $address" >&2
      exit 1
   fi

   cmake -E copy "$build_dir/psu-usb-to-pmbus.uf2" "$release_root/$asset_name"
}

build_firmware 0x58
build_firmware 0x59

asset_58='psu-usb-to-pmbus-rp2040-ax1600i-pmbus-0x58.uf2'
asset_59='psu-usb-to-pmbus-rp2040-ax1600i-pmbus-0x59.uf2'

if cmp -s "$release_root/$asset_58" "$release_root/$asset_59"; then
  echo 'Address 0x58 and 0x59 firmware images are unexpectedly identical.' >&2
  exit 1
fi

(
  cd "$release_root"
  sha256sum "$asset_58" "$asset_59" > SHA256SUMS.txt
)

printf 'Release firmware:\n  %s\n  %s\n' \
  "$release_root/$asset_58" "$release_root/$asset_59"
