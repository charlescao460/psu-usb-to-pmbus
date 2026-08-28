[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ToolchainRoot = Join-Path $ProjectRoot 'toolchain'
$ArmRoot = Join-Path $ToolchainRoot 'arm'
$SdkRoot = Join-Path $ToolchainRoot 'pico-sdk'
$PicoToolsRoot = Join-Path $ToolchainRoot 'pico-tools'
$Downloads = Join-Path $ToolchainRoot 'downloads'
$ArmArchive = Join-Path $Downloads 'ATfE-22.1.0-Windows-x86_64.zip'
$ArmUrl = 'https://github.com/arm/arm-toolchain/releases/download/release-22.1.0-ATfE/ATfE-22.1.0-Windows-x86_64.zip'
$ArmSha256 = '9db55e43d5fdb1619692259897bf1e64f61da82a558827aae6928a2ccb39d348'
$SdkArchive = Join-Path $Downloads 'pico-sdk-2.3.0.zip'
$SdkUrl = 'https://github.com/raspberrypi/pico-sdk/archive/refs/tags/2.3.0.zip'
$SdkToolsArchive = Join-Path $Downloads 'pico-sdk-tools-2.3.0-x64-win.zip'
$SdkToolsUrl = 'https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.3.0-0/pico-sdk-tools-2.3.0-x64-win.zip'
$SdkToolsSha256 = '475f3102e88cb6d12d8984168f395613f361fdd4d4eb338ef1112ca864644c7d'
$PicotoolArchive = Join-Path $Downloads 'picotool-2.3.0-x64-win.zip'
$PicotoolUrl = 'https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.3.0-0/picotool-2.3.0-x64-win.zip'
$PicotoolSha256 = '4dcad3bfbc9d126bdb3870bbce0668f5d300d0f2f505ce775b3444bfdd5eaa79'

New-Item -ItemType Directory -Force -Path $Downloads | Out-Null

if (-not (Test-Path (Join-Path $ArmRoot 'bin\clang.exe'))) {
    if (-not (Test-Path $ArmArchive)) {
        Write-Host "Downloading Arm Toolchain for Embedded 22.1.0..."
        Invoke-WebRequest -Uri $ArmUrl -OutFile $ArmArchive
    }
    $ActualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $ArmArchive).Hash.ToLowerInvariant()
    if ($ActualHash -ne $ArmSha256) {
        throw "Arm toolchain checksum mismatch: expected $ArmSha256, got $ActualHash"
    }
    $ExtractRoot = Join-Path $ToolchainRoot 'arm-extract'
    if (Test-Path $ExtractRoot) {
        Remove-Item -LiteralPath $ExtractRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $ExtractRoot | Out-Null
    Expand-Archive -LiteralPath $ArmArchive -DestinationPath $ExtractRoot
    $Clang = Get-ChildItem -LiteralPath $ExtractRoot -Filter clang.exe -Recurse | Select-Object -First 1
    if (-not $Clang) { throw 'Extracted archive does not contain clang.exe' }
    $ExtractedRoot = Split-Path -Parent (Split-Path -Parent $Clang.FullName)
    if (Test-Path $ArmRoot) { Remove-Item -LiteralPath $ArmRoot -Recurse -Force }
    Move-Item -LiteralPath $ExtractedRoot -Destination $ArmRoot
    Remove-Item -LiteralPath $ExtractRoot -Recurse -Force
}

if (-not (Test-Path (Join-Path $SdkRoot 'pico_sdk_init.cmake'))) {
    if (-not (Test-Path $SdkArchive)) {
        Write-Host 'Downloading Pico SDK 2.3.0...'
        Invoke-WebRequest -Uri $SdkUrl -OutFile $SdkArchive
    }
    $SdkExtract = Join-Path $ToolchainRoot 'pico-sdk-extract'
    if (Test-Path $SdkExtract) { Remove-Item -LiteralPath $SdkExtract -Recurse -Force }
    Expand-Archive -LiteralPath $SdkArchive -DestinationPath $SdkExtract
    $ExtractedSdk = Join-Path $SdkExtract 'pico-sdk-2.3.0'
    if (-not (Test-Path (Join-Path $ExtractedSdk 'pico_sdk_init.cmake'))) {
        throw 'Pico SDK archive has an unexpected layout'
    }
    if (Test-Path $SdkRoot) { Remove-Item -LiteralPath $SdkRoot -Recurse -Force }
    Move-Item -LiteralPath $ExtractedSdk -Destination $SdkRoot
    Remove-Item -LiteralPath $SdkExtract -Recurse -Force
}

if (-not (Test-Path (Join-Path $PicoToolsRoot 'pioasm\pioasm.exe')) -or
    -not (Test-Path (Join-Path $PicoToolsRoot 'picotool\picotool.exe'))) {
    foreach ($Asset in @(
        @{ Path=$SdkToolsArchive; Url=$SdkToolsUrl; Hash=$SdkToolsSha256 },
        @{ Path=$PicotoolArchive; Url=$PicotoolUrl; Hash=$PicotoolSha256 })) {
        if (-not (Test-Path $Asset.Path)) { Invoke-WebRequest -Uri $Asset.Url -OutFile $Asset.Path }
        $ActualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Asset.Path).Hash.ToLowerInvariant()
        if ($ActualHash -ne $Asset.Hash) {
            throw "Pico host tool checksum mismatch for $($Asset.Path): got $ActualHash"
        }
    }
    New-Item -ItemType Directory -Force -Path $PicoToolsRoot | Out-Null
    Expand-Archive -LiteralPath $SdkToolsArchive -DestinationPath $PicoToolsRoot -Force
    Expand-Archive -LiteralPath $PicotoolArchive -DestinationPath $PicoToolsRoot -Force
}

Write-Host "Arm toolchain: $ArmRoot"
Write-Host "Pico SDK:      $SdkRoot"
Write-Host "Pico tools:    $PicoToolsRoot"
