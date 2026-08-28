# psu-usb-to-pmbus

`psu-usb-to-pmbus` converts telemetry from a power supply's USB interface into a
read-only PMBus target. It is intended for systems that can monitor a PMBus power
supply but need to obtain the data from a vendor-specific USB protocol.

The current implementation supports the Corsair AX1600i and the Adafruit Feather
RP2040 USB Host. The transport, PSU protocol, telemetry store, and PMBus server are
kept separate so more MCUs, boards, and PSUs can be added without redesigning the
whole firmware.

> [!IMPORTANT]
> This is an experimental, telemetry-only implementation. It is compatible with
> the useful monitoring portion of the PMBus AC/DC server power-supply profile,
> but it is not profile-certified. It does not provide a physical SMBAlert signal
> and it never forwards PMBus control writes to the PSU.

## Architecture

![Example hardware connection using RP2040](doc/images/rp2040_feather.png)

```mermaid
flowchart LR
   PSU["Power Supply Unit\n(e.g. Corsair AX1600i)"] -->|"USB"| MCU["MCU Dev Board\n(e.g. RP2040)"]
   MCU <-->|"I2C / PMBus"| BMC[" Motherboard\nBMC or PMBus controller"]
   
```

USB polling and PMBus transactions run independently. The I2C target serves only
complete, atomically published snapshots and never waits for a USB exchange. A
short USB interruption preserves the last complete snapshot; after ten seconds
without a successful USB operation, telemetry reads return zero and status reports
OFF rather than inventing input or communications faults.

## Supported hardware

| Component | Current implementation | Status |
|---|---|---|
| MCU and board | [RP2040 / Adafruit Feather RP2040 USB Host](doc/mcu/RP2040.md) | Supported |
| PSU | [Corsair AX1600i](doc/psu/Corsair_AX1600i.md) | Supported |
| PMBus test controller | [Generic Arduino Serial-to-I2C bridge](tools/PMBusTestHost/README.md) | Supported |

The default PMBus target address is `0x58`. The bus runs at 100 kHz with PEC. See
the MCU guide for the board pinout and the PSU guide for protocol and telemetry
details.

## Documentation

- [RP2040 platform guide](doc/mcu/RP2040.md) — wiring, dependencies, build output,
  and platform implementation notes.
- [Corsair AX1600i backend](doc/psu/Corsair_AX1600i.md) — USB protocol, register
  map, polling, recovery, and PMBus page mapping.
- [PMBus command support](doc/PMBUS_COMMANDS.md) — command-by-command coverage,
  data sources, intentional deviations, and application-profile status.
- [BMC compatibility FAQ](doc/FAQ_BMC.md) — discovery behavior, common symptoms,
  and the Gigabyte MZ73-LM0 case study.
- [PMBusTestHost](tools/PMBusTestHost/README.md) — Arduino bridge protocol, wiring,
  compilation, and Python-based hardware verification.
- [Agent guidance](AGENTS.md) — repository invariants and contribution rules for
  coding agents.

## Safety

PMBus is connected directly to MCU GPIO. Use a common ground and external
2.2–4.7 kOhm pull-ups to **3.3 V** (most boards already have them). Never pull SDA or SCL to 5 V. Confirm your
board's voltage tolerance and pin assignments before connecting a motherboard,
PSU, or test controller.

This project exposes telemetry only. Fan commands and every other PSU-control
write are rejected or handled as local compatibility operations; no PMBus write
is sent to the USB-connected PSU.

## Install a prebuilt release

No compiler or embedded-development tools are required. Download a UF2 image from
the [latest GitHub Release](https://github.com/charlescao460/psu-usb-to-pmbus/releases/latest).
Do not download GitHub's automatically generated source-code archives.

### Materials
* [Adafruit Feather RP2040 with USB Type A Host](https://www.adafruit.com/product/5723)
* [4-pin I2C To Headers Cable](https://www.adafruit.com/product/4397)

### Choose the PMBus address

Each release contains two firmware images:

| Firmware file | Use it for |
|---|---|
| `psu-usb-to-pmbus-rp2040-ax1600i-pmbus-0x58.uf2` | The first or only PSU; `0x58` is the normal default. |
| `psu-usb-to-pmbus-rp2040-ax1600i-pmbus-0x59.uf2` | A second PSU sharing the same PMBus wires. |

Each USB-connected PSU currently needs its own Feather bridge. For two PSUs, use
two Feather boards and flash one with the `0x58` image and the other with the
`0x59` image. Never connect two PMBus targets using the same address. Changing
the address later is safe: simply flash the other UF2 image.

### Flash the Feather

1. Download the appropriate `.uf2` file from the latest release.
2. Disconnect the Feather from the motherboard PMBus header before flashing.
3. Connect the Feather's programming USB port to a computer while holding its
   **BOOTSEL** button. Alternatively, hold BOOTSEL, briefly press **RESET**, then
   release BOOTSEL.
4. Wait for a removable drive named **RPI-RP2** to appear.
5. Copy the downloaded `.uf2` file onto **RPI-RP2**. The drive disappearing is
   normal: the Feather automatically reboots into the new firmware.
6. Disconnect programming USB if it is not needed, then connect the AX1600i to the
   Feather's USB-A host port.
7. Reconnect PMBus SDA, SCL, and ground according to the
   [RP2040 wiring guide](doc/mcu/RP2040.md). 

The release also includes `SHA256SUMS.txt` for optional download-integrity
verification. If the Feather does not appear as **RPI-RP2**, disconnect it and
repeat step 3 while continuing to hold BOOTSEL during connection or reset.

## Build from source

Prerequisites are CMake 3.25 or newer, Ninja, Git, and either PowerShell 7 or
Bash. The bootstrap scripts download verified project-local copies of Arm
Toolchain for Embedded 22.1.0 and Pico SDK 2.3.0 into the ignored `toolchain/`
directory. TinyUSB 0.21.0 and its pinned Pico-PIO-USB dependency are fetched by
CMake.

Windows:

```powershell
./scripts/bootstrap-toolchain.ps1
./scripts/build.ps1
```

Linux:

```bash
./scripts/bootstrap-toolchain.sh
./scripts/build.sh
```

The default firmware artifacts are generated under `build/rp2040-ax1600i/`.
See the [RP2040 guide](doc/mcu/RP2040.md) for flashing instructions.

Important CMake cache variables are:

| Variable | Default | Purpose |
|---|---|---|
| `PSU_USB_TO_PMBUS_MCU` | `RP2040` | MCU/platform implementation |
| `PSU_USB_TO_PMBUS_BOARD` | `ADAFRUIT_FEATHER_RP2040_USB_HOST` | Board configuration |
| `PSU_USB_TO_PMBUS_PSU` | `CORSAIR_AX1600I` | PSU backend |
| `PSU_USB_TO_PMBUS_PMBUS_ADDRESS` | `0x58` | 7-bit PMBus target address |
| `PSU_USB_TO_PMBUS_LOG_LEVEL` | `2` | Firmware logging level |

Selections are resolved at configure time. The firmware does not use heap-backed
runtime target discovery.

### Publish a firmware release

Repository maintainers can open **Actions**, select **Build and release RP2040
firmware**, and choose **Run workflow**. The workflow builds both address variants
on Ubuntu 26.04 and, only after both succeed, creates a release tagged with a UTC
timestamp. The two UF2 files and their SHA-256 checksum file are attached to that
release.

## Test

Build the portable C++ protocol tests:

```powershell
./scripts/build.ps1 -HostTests
```

Create the Python virtual environment and run its unit tests:

```powershell
./scripts/bootstrap-tests.ps1
./.venv/Scripts/python.exe -m pytest tests/python
```

After flashing the Arduino bridge, pass its serial port explicitly to the live
test tools:

```powershell
$BridgePort = "YOUR_SERIAL_PORT"
./.venv/Scripts/python.exe -m tools.pmbus_test scan --port $BridgePort
./.venv/Scripts/python.exe -m tools.pmbus_test verify --port $BridgePort --address 0x58
./.venv/Scripts/python.exe -m tools.pmbus_test monitor --port $BridgePort --address 0x58
```

Hardware-specific validation and the captured motherboard compatibility test are
documented in [PMBusTestHost](tools/PMBusTestHost/README.md).

## PMBus behavior

- Pages `0x00`–`0x02` expose the aggregate 12 V, 5 V, and 3.3 V rails.
- Pages `0x10`–`0x1B` expose AX1600i branch channels 0–11.
- Input power comes directly from the AX1600i native `0x97` register; it is never
  fabricated from an efficiency curve.
- Aggregate output power follows the iCUE behavior of `VOUT × IOUT` for each rail.
- `READ_EIN` implements the AC/DC profile's Direct-format input-power accumulator,
  rollover count, and aligned sample count; it is not a watt-hour value.
- Discovery and safe compatibility writes may update local PMBus state, but no
  write reaches the PSU and CML status is never left sticky.

See the [AX1600i backend guide](doc/psu/Corsair_AX1600i.md) for the complete
implementation details, the [PMBus command matrix](doc/PMBUS_COMMANDS.md) for
precise command coverage, and [BMC compatibility FAQ](doc/FAQ_BMC.md) for
controller quirks.

## Porting

The project has two implementation extension points—the MCU/platform layer and
the PSU backend—and three configure-time selections:

| Selection | Chooses |
|---|---|
| `PSU_USB_TO_PMBUS_MCU` | MCU family, SDK, and low-level platform implementation |
| `PSU_USB_TO_PMBUS_BOARD` | Board pinout, USB-host wiring, and board-specific initialization |
| `PSU_USB_TO_PMBUS_PSU` | PSU USB protocol, polling state machine, and telemetry mapping |

Keep these selections static so the firmware can use bounded storage and avoid
runtime target discovery. The portable PMBus and telemetry layers communicate
with every port through `UsbTransport`, `PsuBackend`, and `TelemetrySnapshot`.

### Add an MCU or board

1. Choose and pin the compiler, MCU SDK, USB-host stack, and any code-generation
   tools. Bootstrap them into ignored `toolchain/` storage with version and
   checksum verification; do not depend on one developer's global installation.
2. If the SDK or toolchain selects the C/C++ compilers, add its import or toolchain
   file before the top-level `project()` call. Keep SDK initialization and
   dependencies conditional on the selected MCU so host tests need no embedded
   toolchain.
3. Add the MCU and board values to `cmake/SelectTarget.cmake`, validate unsupported
   combinations there, and translate the public board selection into the SDK's
   board identifier and pin configuration.
4. Add `src/platform/<mcu>/` implementations for asynchronous `UsbTransport`, the
   non-blocking PMBus/I2C target, board initialization, USB-host pins, timing, and
   a stable platform identifier.
5. In `CMakeLists.txt`, select only that platform's sources, include paths, compile
   definitions, linker flags, SDK libraries, and firmware-output helpers. Do not
   introduce MCU headers or libraries into `psu_usb_to_pmbus_core`.
6. Add an isolated configure/build preset and update both PowerShell and Bash
   bootstrap/build flows. A fresh checkout should be able to bootstrap, configure,
   build, and locate its firmware without manual environment setup.
7. Keep portable logic covered by host tests, then validate USB, I2C recovery, and
   electrical behavior on hardware. Document the port in `doc/mcu/<MCU>.md` and
   link it from the supported-hardware table.

### Add a PSU

1. Implement the `PsuBackend` contract under `src/psu/<vendor_model>/` and keep its
   wire protocol isolated from platform USB code.
2. Decode USB replies into a complete `TelemetrySnapshot`; do not let the PMBus
   handler query the PSU directly.
3. Define polling cadence, reply validation, detach/recovery behavior, page/rail
   mapping, identity fields, and stale-data behavior.
4. Add the new `PSU_USB_TO_PMBUS_PSU` value and make CMake select only the chosen
   backend sources and target-specific compile definitions. Keep PSU protocol
   sources host-buildable whenever they do not require MCU APIs.
5. Add focused protocol, conversion, malformed-reply, timeout, and recovery tests.
   If the backend changes PMBus coverage or semantics, update
   `doc/PMBUS_COMMANDS.md`.
6. Document verified commands, USB identity, timing, data sources, and limitations
   in `doc/psu/<PSU>.md`, then update the supported-hardware table.

### CMake and toolchain checklist

A port is not complete until its build is reproducible from a clean checkout:

| Area | Required integration |
|---|---|
| Target selection | Extend `cmake/SelectTarget.cmake` and reject invalid MCU/board/PSU combinations with clear errors. |
| Compiler and SDK | Pin versions; import compiler-selecting SDK/toolchain logic before `project()`; initialize the selected SDK afterward. |
| Dependencies | Fetch or import only the dependencies needed by the selected target, with immutable release tags or commits. |
| Firmware target | Conditionally select sources, definitions, libraries, linker options, and output generation in `CMakeLists.txt`. |
| Host tests | Preserve `PSU_USB_TO_PMBUS_BUILD_FIRMWARE=OFF` so portable protocol and telemetry tests build with a normal host compiler. |
| Presets | Add named configure and build presets with a separate `build/<preset>/` directory for each supported combination. |
| Bootstrap scripts | Keep PowerShell and Bash behavior equivalent, downloads verified, operations idempotent, and generated files under ignored directories. |
| Documentation | Record prerequisites, exact build/flash commands, artifacts, pins, and target limitations in the corresponding MCU or PSU guide. |

All ports must retain the read-only policy, bounded static storage, atomic
snapshot publication, and non-blocking I2C handling described in [AGENTS.md](AGENTS.md).

## Repository layout

```text
cmake/          dependency and target selection
doc/mcu/        one implementation guide per MCU/platform
doc/psu/        one protocol guide per supported PSU
include/        stable firmware interfaces and data types
src/platform/   MCU and board implementations
src/psu/        PSU backends and wire protocols
src/pmbus/      PMBus command and response handling
src/telemetry/  snapshot storage and derived telemetry
tests/          portable C++ and Python tests
tools/          Arduino bridge and Python PMBus test host
```

## Credits
* [thad0ctor/corsair-top](https://github.com/thad0ctor/corsair-top)
* [Jon0/ax1600i](https://github.com/Jon0/ax1600i)

## License

This project is available under the [MIT License](LICENSE).
