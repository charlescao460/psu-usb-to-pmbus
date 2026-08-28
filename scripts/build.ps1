[CmdletBinding()]
param([switch]$HostTests)
$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $ProjectRoot
try {
    if ($HostTests) {
        $NativeCompiler = Get-Command cl, clang++, g++ -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($NativeCompiler) {
            cmake --preset host-tests
            cmake --build --preset host-tests
            ctest --preset host-tests
        } elseif (Get-Command wsl.exe -ErrorAction SilentlyContinue) {
            $WslRoot = (& wsl.exe -e wslpath -a $ProjectRoot).Trim()
            if ($WslRoot.Contains("'")) { throw 'Project paths containing a single quote are unsupported.' }
            $WslRootQuoted = "'$WslRoot'"
            & wsl.exe -e bash -lc "cd $WslRootQuoted && cmake -S . -B build/host-tests-wsl -G Ninja -DPSU_USB_TO_PMBUS_BUILD_FIRMWARE=OFF -DBUILD_TESTING=ON && cmake --build build/host-tests-wsl && ctest --test-dir build/host-tests-wsl --output-on-failure"
            if ($LASTEXITCODE -ne 0) { throw 'WSL host tests failed' }
        } else {
            throw 'Host tests require cl, clang++, g++, or WSL with a C++ compiler.'
        }
    } else {
        $env:PICO_SDK_PATH = Join-Path $ProjectRoot 'toolchain\pico-sdk'
        $env:PICO_TOOLCHAIN_PATH = Join-Path $ProjectRoot 'toolchain\arm'
        $Pioasm = Join-Path $ProjectRoot 'toolchain\pico-tools\pioasm'
        $Picotool = Join-Path $ProjectRoot 'toolchain\pico-tools\picotool'
        cmake --preset rp2040-ax1600i "-Dpioasm_DIR=$Pioasm" "-Dpicotool_DIR=$Picotool"
        cmake --build --preset rp2040-ax1600i
    }
} finally { Pop-Location }
