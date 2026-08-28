# CorsairUSB2PMBus agent notes

## Non-negotiable design rules

- Firmware is C++20 bare metal: no heap allocation after startup, exceptions, RTTI, or blocking USB work in the I2C target handler.
- PMBus is read-only. Never forward PMBus writes (especially `FAN_COMMAND_1`) to the PSU.
- Non-control compatibility writes may update local PMBus state or be accepted as an explicit no-op; they must never reach the PSU.
- The I2C target serves only a complete, atomically published `TelemetrySnapshot`. USB callbacks/state never run inside the I2C handler.
- Motherboard-compatibility exception: never leave CML sticky. Discovery probes and safely ignored writes do not set it; malformed known traffic/PEC errors are one-shot and consumed by the first status read. Preserve the last complete snapshot for ten seconds of continuous USB failure; after that, return zero telemetry and assert OFF only (never INPUT or CML).
- `READ_EIN` is the AC/DC profile's Direct-format input-power accumulator (`Paccum`, rollover, aligned 24-bit sample count), not watt-hours.
- Do not edit generated content in `build/`, `toolchain/`, `.venv/`, or `test-results/`.

## Current hardware and pinned dependencies

- Board: Adafruit Feather RP2040 USB Host; USB D+ GPIO16, D- GPIO17, VBUS enable GPIO18.
- PMBus: I2C1, SDA GPIO2, SCL GPIO3, 100 kHz, 7-bit address `0x58`, external 2.2-4.7 kOhm pull-ups to 3.3 V, common ground. Never apply 5 V to the I2C pins.
- Feather debug USB currently enumerates as `COM11` on this workstation (port numbers may change). Arduino Due Native USB test bridge: `COM8`, FQBN `arduino:sam:arduino_due_x`; its Bossa programming port was observed as `COM9` during reset.
- Pico SDK 2.3.0; TinyUSB 0.21.0; Pico-PIO-USB commit `675543bcc9baa8170f868ab7ba316d418dbcf41f`; ATfE LLVM 22.1.0; Ninja generator.
- `cmake/patches/pico-pio-usb-clang-crc-inline.patch` is required: without it ATfE Clang outlines the timing-critical CRC update and the AX1600i misses bulk-IN ACKs. Keep the patch synchronized with the pinned Pico-PIO-USB revision.
- Default CMake selections: RP2040, CORSAIR_AX1600I, ADAFRUIT_FEATHER_RP2040_USB_HOST, PMBus `0x58`.

## AX1600i protocol facts

- USB VID:PID `1b1c:1c11`, interface 0, bulk OUT `0x02`, bulk IN `0x82`.
- Enable bridge with vendor control request `40 02`, value `0x0002`, index 0; initialize with decoded payload `11 02 64 00 00 00 00`.
- Encoding maps each low nibble then high nibble through `55 56 59 5a 65 66 69 6a 95 96 99 9a a5 a6 a9 aa`, with an encoded command byte and zero terminator.
- Replies are asymmetric: the zero terminator decodes as a trailing zero byte, and a terminator-only record decodes as an empty transient response. Retry an empty data record locally and preserve the last fresh telemetry through link recovery or detach; PMBus invalidates it only after ten seconds without a successful USB operation.
- Treat USBXpress vendor-control setup as a per-attachment operation. Operational timeouts, empty records, or malformed acknowledgements discard the current sample and locally restart the Corsair register sequence. Only ten seconds of uninterrupted operational failure may flush/reopen an attached bridge; any successful operation clears that timer.
- Leave 100 ms of quiet time between complete Corsair register operations. Keep the three-message register handshake at 5 ms settling cadence, interleave primary VIN/IIN/PIN reads through the slower telemetry walk, and never restart the whole walk after an ordinary read failure. PMBus always serves cached snapshots and does not require request-coupled USB reads.
- Register read is three exchanges: `13 03 06 01 07 <len> <reg>`, `12`, then `08 07 <len>`.
- Important registers: name `9A`, VIN `88`, IIN `89`, native PIN `97`, total DC output power `EE`, VOUT `8B`, IOUT `8C`, POUT `96`, temperatures `8E/8D`, fan `90`, uptime `D2`, rail selector `00`, branch selector `E7`, branch I/P/OCP `E8/E9/EA`.
- Validate every register header acknowledgement as decoded `{0}` and every trigger acknowledgement as decoded `{0, 0}` before advancing. A malformed acknowledgement must never be treated as register data.
- A read-only `00`-`FF` scan found no credible standalone efficiency register. Do not use an efficiency curve to fabricate input power: `READ_PIN` comes directly from `97`.
- iCUE is the power-display reference: aggregate-page `READ_POUT` is `VOUT * IOUT`, and total output is the sum of the 12 V, 5 V, and 3.3 V products. Keep native `96`/`EE` readings in diagnostic fields only; do not expose either as PMBus output power. Efficiency, when needed for diagnostics, is derived output divided by contemporaneous native `97` input.
- `MFR_SERIAL` is the RP2040's uppercase 16-digit unique board ID. The PSU backend must preserve platform identity fields when publishing telemetry snapshots.
- Live MZ73 BMC polling writes `1B 81 FF` (`SMBALERT_MASK`), `05 04 01 1B 7C <mask>` (`PAGE_PLUS_WRITE` wrapping `SMBALERT_MASK`), and `D0 00`, all with valid PEC, before reading `97`. Keep masks local and accept the exact `D0 00` transaction as a no-op; none may set CML or be forwarded to the PSU.
- MZ73 cold-start discovery reads `MFR_POUT_MAX` (`A7`) and uses it as PSU capacity. Report the AX1600i's rated 1600 W in LINEAR11; returning an unsupported-read fill such as `FFFF` makes the BMC advertise a bogus 65535 W capacity.
- On BMC firmware 13.06.27, cold discovery populates the Corsair/AX1600i FRU and polls valid `READ_PIN` values about once per second. The exact `MZ73-LM0-000.xml` devmap defines `PSU_POWER` at `0x58`/`0x59`, command `0x97`, link 13, but all six MZ73 devmaps omit the derived `SYS_POWER` sensor. Working Gigabyte devmaps add `SUM __taker__="13"`, `DIVIDE divisor="25"`, and sensor `0xE9`/`SYS_POWER`; `libgbt.so::Check_get_sys_power_no` explicitly requires that entry. The live BMC now has an external model patch at version `1781612875` containing that block; API and Web UI validation produced 400-425 W. Do not add nonstandard PMBus encoding workarounds for this BMC devmap defect.
- Reusable BMC patch code, documentation, and tests are maintained in sibling repository `../MZ73-LM0-BMC-Patch`; raw firmware analysis and captures remain under ignored `local-debug/bmc-fw-13.06.27/`. They are specific to MZ73-LM0-000 firmware 13.06.27. The identified rollback endpoint is `GET /api/maintenance/clear_model_patch`, followed by a BMC reboot; never install this artifact on a different model or firmware revision.

## Validation

- Host C++: `scripts/build.ps1 -HostTests`.
- Python: `.venv/Scripts/python -m pytest tests/python`.
- Arduino compile targets: Uno, Due Native USB, Arduino Zero Native USB. Live upload uses `COM8`.
- Firmware: run `scripts/bootstrap-toolchain.ps1`, then `scripts/build.ps1`; UF2 is under `build/rp2040-ax1600i/`.
- Diagnostic register scan: build preset `rp2040-ax1600i-scan`. It performs only two-byte reads and logs `AXSCAN` records, but unknown addresses may return patterned data and must not be assigned semantics without independent validation.
- Hardware verification uses `python -m tools.pmbus_test verify --port COM8 --address 0x58` and compares with Feather logs on its current debug port (`COM11` at last validation).
