#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
toolchain_root="$project_root/toolchain"
downloads="$toolchain_root/downloads"
arm_root="$toolchain_root/arm"
sdk_root="$toolchain_root/pico-sdk"
pico_tools_root="$toolchain_root/pico-tools"
mkdir -p "$downloads"

case "$(uname -m)" in
  x86_64)
    arm_name="ATfE-22.1.0-Linux-x86_64.tar.xz"
    arm_sha="e2e9e637bba097ba6e4bae6982883fe705ffd7e8c3a7dc876964835ef1c7a724"
    sdk_tools_platform="x86_64-lin"
    sdk_tools_sha="c4b3597875e0253a6d92b935913ef4299ad3964d9b0c87eec1ee8566b93fbe8e"
    picotool_sha="d8222dbb04e83427bcaef8466fe6e76b0e0193c3a140029934bd365dae49f61f"
    ;;
  aarch64|arm64)
    arm_name="ATfE-22.1.0-Linux-AArch64.tar.xz"
    arm_sha="ca73e75963bb90c4bc66d67b1875a05e120a8a4febf3db0ad5f09330fbffda17"
    sdk_tools_platform="aarch64-lin"
    sdk_tools_sha="4dae3609a58ca89c0e2ae863dace3f57d2558a3ea7789ffad02dec377a309d64"
    picotool_sha="90fc7e939a68f33b286f9fc4eaa58c11b49c4251c472d541c73d211ec28b8922"
    ;;
  *) echo "Unsupported host architecture: $(uname -m)" >&2; exit 1 ;;
esac

arm_archive="$downloads/$arm_name"
arm_url="https://github.com/arm/arm-toolchain/releases/download/release-22.1.0-ATfE/$arm_name"
if [[ ! -x "$arm_root/bin/clang" ]]; then
  [[ -f "$arm_archive" ]] || curl -fL "$arm_url" -o "$arm_archive"
  echo "$arm_sha  $arm_archive" | sha256sum --check -
  extract_root="$(mktemp -d "$toolchain_root/arm-extract.XXXXXX")"
  tar -xJf "$arm_archive" -C "$extract_root"
  clang_path="$(find "$extract_root" -type f -path '*/bin/clang' -print -quit)"
  [[ -n "$clang_path" ]] || { echo 'clang not found in archive' >&2; exit 1; }
  extracted_root="$(dirname "$(dirname "$clang_path")")"
  rm -rf -- "$arm_root"
  mv -- "$extracted_root" "$arm_root"
  rm -rf -- "$extract_root"
fi

sdk_archive="$downloads/pico-sdk-2.3.0.tar.gz"
if [[ ! -f "$sdk_root/pico_sdk_init.cmake" ]]; then
  [[ -f "$sdk_archive" ]] || curl -fL \
    'https://github.com/raspberrypi/pico-sdk/archive/refs/tags/2.3.0.tar.gz' \
    -o "$sdk_archive"
  sdk_extract="$(mktemp -d "$toolchain_root/pico-sdk-extract.XXXXXX")"
  tar -xzf "$sdk_archive" -C "$sdk_extract"
  rm -rf -- "$sdk_root"
  mv -- "$sdk_extract/pico-sdk-2.3.0" "$sdk_root"
  rm -rf -- "$sdk_extract"
fi

if [[ ! -x "$pico_tools_root/pioasm/pioasm" || ! -x "$pico_tools_root/picotool/picotool" ]]; then
  sdk_tools_name="pico-sdk-tools-2.3.0-$sdk_tools_platform.tar.gz"
  picotool_name="picotool-2.3.0-$sdk_tools_platform.tar.gz"
  sdk_tools_archive="$downloads/$sdk_tools_name"
  picotool_archive="$downloads/$picotool_name"
  base_url='https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.3.0-0'
  [[ -f "$sdk_tools_archive" ]] || curl -fL "$base_url/$sdk_tools_name" -o "$sdk_tools_archive"
  [[ -f "$picotool_archive" ]] || curl -fL "$base_url/$picotool_name" -o "$picotool_archive"
  echo "$sdk_tools_sha  $sdk_tools_archive" | sha256sum --check -
  echo "$picotool_sha  $picotool_archive" | sha256sum --check -
  mkdir -p "$pico_tools_root"
  tar -xzf "$sdk_tools_archive" -C "$pico_tools_root"
  tar -xzf "$picotool_archive" -C "$pico_tools_root"
fi

printf 'Arm toolchain: %s\nPico SDK:      %s\nPico tools:    %s\n' \
  "$arm_root" "$sdk_root" "$pico_tools_root"
