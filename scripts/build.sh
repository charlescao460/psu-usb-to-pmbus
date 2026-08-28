#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"
if [[ "${1:-}" == "--host-tests" ]]; then
  cmake --preset host-tests
  cmake --build --preset host-tests
  ctest --preset host-tests
else
  export PICO_SDK_PATH="$project_root/toolchain/pico-sdk"
  export PICO_TOOLCHAIN_PATH="$project_root/toolchain/arm"
  cmake --preset rp2040-ax1600i \
    -Dpioasm_DIR="$project_root/toolchain/pico-tools/pioasm" \
    -Dpicotool_DIR="$project_root/toolchain/pico-tools/picotool"
  cmake --build --preset rp2040-ax1600i
fi
