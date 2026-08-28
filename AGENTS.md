# psu-usb-to-pmbus agent notes

## Repository guidance

- Read the root `README.md` for the public project overview and porting workflow.
- MCU and board details belong in `doc/mcu/<MCU>.md`; PSU protocol details belong
  in `doc/psu/<PSU>.md`.
- Motherboard-specific investigation and compatibility guidance belong in
  `doc/FAQ_BMC.md`, not in the root README.
- `doc/PMBUS_COMMANDS.md` is the authoritative public command matrix. Update it
  whenever command handling, data sources, write policy, pages, or profile
  deviations change.
- `AGENTS.local.md`, when present, contains ignored workstation and lab state.
  Read it when working with connected hardware, but never copy ports, credentials,
  private addresses, absolute paths, or transient observations into tracked files.
- Do not edit generated content in `build/`, `toolchain/`, `.venv/`,
  `test-results/`, or `local-debug/`.

## Non-negotiable design rules

- Firmware is C++20 bare metal: no heap allocation after startup, exceptions,
  RTTI, or blocking USB work in the I2C target handler.
- PMBus is read-only. Never forward PMBus writes, especially `FAN_COMMAND_1`, to
  the PSU.
- Non-control compatibility writes may update local PMBus state or be accepted as
  explicit no-ops; they must never reach the PSU.
- The I2C target serves only a complete, atomically published
  `TelemetrySnapshot`. USB callbacks and state never run inside the I2C handler.
- Never leave CML sticky. Discovery probes and safely ignored writes do not set
  it; malformed known traffic and PEC errors are one-shot and consumed by the
  first status read.
- Preserve the last complete snapshot for ten seconds of continuous USB failure.
  After that, report zero telemetry and OFF only, never INPUT or CML.
- `READ_EIN` is the AC/DC profile's Direct-format input-power accumulator
  (`Paccum`, rollover, aligned 24-bit sample count), not watt-hours.

## Current implementations and dependencies

- Supported MCU/board: RP2040 on the Adafruit Feather RP2040 USB Host. See
  `doc/mcu/RP2040.md` for pins, SDK integration, and flashing.
- Supported PSU: Corsair AX1600i. See `doc/psu/Corsair_AX1600i.md` for protocol,
  registers, timing, and page mapping.
- Default selections are `RP2040`, `CORSAIR_AX1600I`,
  `ADAFRUIT_FEATHER_RP2040_USB_HOST`, and PMBus address `0x58`.
- Pinned dependencies are Pico SDK 2.3.0, TinyUSB 0.21.0, Pico-PIO-USB commit
  `675543bcc9baa8170f868ab7ba316d418dbcf41f`, ATfE LLVM 22.1.0, and Ninja.
- `cmake/patches/pico-pio-usb-clang-crc-inline.patch` is required. Without it,
  ATfE Clang outlines a timing-critical CRC update and the AX1600i misses bulk-IN
  ACKs. Keep the patch synchronized with the pinned Pico-PIO-USB revision.

## AX1600i invariants

- USB VID:PID is `1b1c:1c11`, interface 0, bulk OUT `0x02`, bulk IN `0x82`.
- USBXpress vendor-control setup is per attachment. Ordinary operational failures
  restart only the local register sequence; only ten seconds of uninterrupted
  failure may flush and reopen an attached bridge.
- Leave 100 ms between complete register operations and retain the three-message
  handshake's 5 ms settling cadence.
- Validate every decoded header acknowledgement as exactly `{0}` and every
  decoded trigger acknowledgement as exactly `{0, 0}` before accepting data.
- `READ_PIN` comes directly from native register `0x97`. Never fabricate it from
  an efficiency curve.
- Aggregate-page `READ_POUT` is `VOUT * IOUT`, matching iCUE. Native `0x96` and
  `0xEE` values remain diagnostic fields only.
- `MFR_SERIAL` is the platform's stable uppercase 16-digit identifier. PSU
  snapshot publication must preserve platform identity fields.
- The complete protocol and verified register map are maintained in
  `doc/psu/Corsair_AX1600i.md`.

## PMBus and BMC compatibility

- Logical `SMBALERT_MASK`, `PAGE_PLUS_WRITE`, and safe manufacturer-specific
  discovery writes remain local. The captured `D0 00` transaction is an accepted
  no-op; none of these operations may set sticky CML or reach the PSU.
- Report the AX1600i's rated 1600 W through `MFR_POUT_MAX` in LINEAR11. Some BMCs
  use this during inventory discovery.
- Do not add nonstandard PMBus encoding workarounds for a motherboard firmware or
  sensor-map defect. Record confirmed controller behavior in `doc/FAQ_BMC.md`.
- Keep `doc/PMBUS_COMMANDS.md` synchronized with the command enum and both the
  PMBus server read and write paths.

## Validation

- Host C++: `scripts/build.ps1 -HostTests`.
- Python: `.venv/Scripts/python -m pytest tests/python`.
- Arduino compile targets: Uno, Due Native USB, and Arduino Zero Native USB.
- Firmware: run `scripts/bootstrap-toolchain.ps1`, then `scripts/build.ps1`; the
  default UF2 is under `build/rp2040-ax1600i/`.
- Diagnostic register scan: build preset `rp2040-ax1600i-scan`. Unknown addresses
  may return patterned data and must not receive semantics without independent
  validation.
- Live hardware tests must pass the selected bridge port explicitly, for example
  `python -m tools.pmbus_test verify --port <port> --address 0x58`.
