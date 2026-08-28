# psu-usb-to-pmbus

psu-usb-to-pmbus turns telemetry from a Corsair AX1600i USB connection into a read-only PMBus target. The first platform is an Adafruit Feather RP2040 USB Host, but USB transport, PSU backend, telemetry storage, and PMBus serving are separate interfaces so other MCUs and PSUs can be added.

> This implementation is compatible with the useful telemetry portion of the PMBus AC/DC Server Power Supply profile. It is not profile-certified: there is no SMBAlert wire, control writes are deliberately ignored, and status/energy fields not exposed by Corsair are synthesized.

## Data flow

```text
AX1600i -- vendor USB/CP210x --> TinyUSB PIO host --> AX1600i backend
                                                         |
                                                  telemetry snapshot
                                                         |
PMBus controller <-- I2C1 target / PEC <-- PMBus server <-+
```

The USB side runs independently and atomically publishes complete snapshots. The I2C handler never waits for USB. The last complete snapshot is preserved through ten seconds of continuous USB failure; after that, telemetry reads return zero and status asserts OFF only. USB bridge loss is not misreported as an AC-input or CML fault.

## Hardware

| Signal | Feather RP2040 USB Host | Test host |
|---|---:|---|
| USB D+ / D- | GPIO16 / GPIO17 | AX1600i USB |
| USB VBUS enable | GPIO18 | on-board host power switch |
| PMBus SDA | GPIO2 | Arduino SDA |
| PMBus SCL | GPIO3 | Arduino SCL |
| Ground | GND | Arduino GND |

Fit 2.2-4.7 kOhm pull-ups from SDA and SCL to **3.3 V**. Never pull either signal to 5 V. The default PMBus address is `0x58`, at 100 kHz, with PEC.

The Feather debug serial currently enumerates as `COM11` on the validation workstation; Windows COM assignments can change after reflashing. The Arduino Due Native USB PMBus test bridge is `COM8` (its temporary Bossa programming port was observed as `COM9`).

## Build

Prerequisites are CMake 3.25+, Ninja, Git, and PowerShell 7 or Bash. Bootstrap downloads and verifies Arm Toolchain for Embedded 22.1.0 and installs Pico SDK 2.3.0 under ignored `toolchain/`:

```powershell
./scripts/bootstrap-toolchain.ps1
./scripts/build.ps1
```

Linux:

```bash
./scripts/bootstrap-toolchain.sh
./scripts/build.sh
```

The firmware and UF2 are generated in `build/rp2040-ax1600i/`. Bootstrap also installs the official Pico SDK 2.3.0 host tools so no system C/C++ compiler is needed for `pioasm` or `picotool`. The build uses the Ninja generator, ATfE Clang, Pico SDK 2.3.0, TinyUSB 0.21.0, and the TinyUSB-pinned Pico-PIO-USB revision. CMake applies a small pinned Pico-PIO-USB patch that keeps its receive CRC update inline under Clang; this is required to meet the AX1600i USBXpress bulk-IN ACK timing.

Configuration cache variables include `PSU_USB_TO_PMBUS_MCU`, `PSU_USB_TO_PMBUS_PSU`, `PSU_USB_TO_PMBUS_BOARD`, `PSU_USB_TO_PMBUS_PMBUS_ADDRESS`, and `PSU_USB_TO_PMBUS_LOG_LEVEL`.

The backend reads native AX1600i input power from register `0x97`; no efficiency curve is used to synthesize input power. USB capture correlated with iCUE showed that its visible per-rail power is `VOUT * IOUT`, including the 3.3 V page where native register `0x96` returned zero, and its total output is the sum of the three aggregate rail products. Firmware therefore uses the same arithmetic for PMBus `READ_POUT` and total-output telemetry. Native `0x96` and `0xEE` values remain separate diagnostics and cannot become PMBus power responses. Register command processing validates the decoded `{0}` header acknowledgement and `{0, 0}` trigger acknowledgement before accepting returned data.

The USB backend leaves 100 ms between complete Corsair operations rather than continuously saturating the PIO host endpoints. The three-message register handshake itself retains the reference client's 5 ms settling cadence. VIN, IIN, and native PIN are interleaved through the slower rail/temperature/branch walk, which keeps input-power refresh near iCUE's observed 1.5-second cadence while PMBus continues to answer entirely from its cached snapshot. Empty transient data records and isolated transfer loss discard or retry only the affected sample; they do not reopen USBXpress or restart the telemetry walk.

`MFR_SERIAL` is the RP2040's 16-digit unique board ID. Logical `SMBALERT_MASK` writes and readback are supported directly and through `PAGE_PLUS_WRITE`, even though there is no physical SMBAlert wire; masks are kept locally and are never forwarded to the PSU. The observed Gigabyte MZ73 `MFR_SPECIFIC_D0=0` polling write is also accepted as a local no-op so normal BMC discovery cannot create a sticky CML fault.

The MZ73 also reads `MFR_POUT_MAX` (`0xA7`) during cold-start discovery, so the AX1600i backend reports its rated 1600 W output in LINEAR11. Live validation populated the BMC FRU with `Corsair`, `AX1600i`, `USB-PMBus`, and the complete RP2040 serial, and the BMC then read meaningful native `READ_PIN` values once per second.

Reverse engineering the exact Gigabyte 13.06.27 image identified why `/api/dcmi/power` and Redfish `PowerConsumedWatts` originally remained zero. The active `MZ73-LM0-000.xml` devmap defines raw `PSU_POWER` producers for PMBus addresses `0x58` and `0x59`, both using command `0x97` and link 13, but every MZ73 devmap in the image omits the derived `SYS_POWER` sensor. Gigabyte platforms that expose chassis power add a `SUM` taker for that link, divide by 25, and publish sensor `0xE9` as `SYS_POWER`. `libgbt.so`'s `Check_get_sys_power_no` scans `/tmp/devmap.xml` specifically for `sdr="SYS_POWER"`; without it, the DCMI path has no aggregate sensor even though the two PSU readings are valid. This is an MZ73 devmap defect, not a PSU-vendor gate or a LINEAR11 incompatibility, and it cannot be corrected by changing the PMBus target.

The diagnosis was validated on the target board with Gigabyte's supported external model-patch mechanism: a minimally changed, version-incremented devmap added the missing `SYS_POWER` block without modifying the base BMC image or FRU data. After the BMC rebooted, sensor `0xE9` reported 425 W through the API, `/api/dcmi/power` reported 425 W, and the Web UI's System Power Consumption page displayed meaningful 400-425 W readings. Reusable patch tooling, safety checks, documentation, and tests live in the sibling `MZ73-LM0-BMC-Patch` repository. Raw firmware analysis and captures remain under ignored `local-debug/`. The patch is specific to MZ73-LM0-000 firmware 13.06.27 and must not be installed on another model or firmware version without repeating the analysis.

For motherboard compatibility, unsupported discovery reads and locally ignored writes do not set CML. Malformed known transactions and bad PEC may produce a one-shot CML detail, but the first `STATUS_BYTE`, `STATUS_WORD`, or `STATUS_CML` observation consumes it; it can never persist across status polls. `CLEAR_FAULTS` and writable status-clear commands are accepted locally, including command-only `CLEAR_FAULTS` and unambiguous writes without PEC. Manufacturer-specific writes `0xC4`-`0xFD` are safe local no-ops, and broadcast `PAGE=0xFF` is accepted without disrupting the last readable telemetry page. No PMBus write is ever forwarded to the AX1600i. Canonical counted process-call framing is implemented for `QUERY` and `COEFFICIENTS`, while the legacy uncounted form remains accepted for older tools.

`READ_EIN` follows the AC/DC Server Profile accumulator semantics rather than reporting watt-hours. Each fresh native AX1600i `READ_PIN` (`0x97`) sample is rounded to Direct-format integer watts and added to a two-byte `Paccum` that rolls at `0x7FFF`; the response also contains an 8-bit rollover count and the aligned 24-bit sample count. All three fields are published atomically and retained when ordinary telemetry becomes stale.

The live `verify` command asserts this iCUE power relationship on aggregate pages `0x00`-`0x02`, allowing only LINEAR11 quantization tolerance.

For read-only protocol exploration, `PSU_USB_TO_PMBUS_AX1600I_REGISTER_SCAN=ON` (or preset `rp2040-ax1600i-scan`) scans `0x00` through `0xFF` using two-byte reads and emits `AXSCAN` records. Unknown addresses can return patterned values rather than errors, so scan output is diagnostic evidence, not an automatic register map. The live AX1600i scan found no credible standalone efficiency register.

## Tests

Build the portable C++ protocol tests:

```powershell
./scripts/build.ps1 -HostTests
```

Create the required Python virtual environment and run unit tests:

```powershell
./scripts/bootstrap-tests.ps1
./.venv/Scripts/python.exe -m pytest tests/python
```

After flashing the Arduino sketch in `tools/PMBusTestHost`, scan or verify the live target:

```powershell
./.venv/Scripts/python.exe -m tools.pmbus_test scan --port COM8
./.venv/Scripts/python.exe -m tools.pmbus_test monitor --port COM8 --address 0x58
./.venv/Scripts/python.exe -m tools.pmbus_test verify --port COM8 --address 0x58
./.venv/Scripts/python.exe -m tools.pmbus_test reference --port COM8 --address 0x58
```

The `reference` run replays the exact non-control writes captured from the live
MZ73 BMC, checks native AX1600i input power and rated capacity, and validates the
iCUE aggregate-rail power relationship. See `tools/PMBusTestHost/README.md` for
the complete reference contract, bridge protocol, wiring, and Arduino
build/upload commands.
