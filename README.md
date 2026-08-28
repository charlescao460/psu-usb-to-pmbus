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
2.2–4.7 kOhm pull-ups to **3.3 V**. Never pull SDA or SCL to 5 V. Confirm your
board's voltage tolerance and pin assignments before connecting a motherboard,
PSU, or test controller.

This project exposes telemetry only. Fan commands and every other PSU-control
write are rejected or handled as local compatibility operations; no PMBus write
is sent to the USB-connected PSU.

## Build

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

The project has two primary extension points: the MCU/platform layer and the PSU
backend. Keep new implementations statically selected through CMake and preserve
the boundary formed by `TelemetrySnapshot`.

### Add an MCU or board

1. Add a platform directory under `src/platform/<mcu>/` implementing asynchronous
   `UsbTransport` and a non-blocking PMBus/I2C target.
2. Provide board initialization, USB-host pin configuration, PMBus pins, timing,
   and a stable hardware identifier without leaking platform callbacks into the
   PMBus server.
3. Add the MCU, board, SDK, and source selection to `cmake/SelectTarget.cmake` and
   the top-level CMake target.
4. Add host-testable logic where possible and validate I2C behavior with a real
   controller or logic analyzer.
5. Document the implementation in `doc/mcu/<MCU>.md` and link it from the supported
   hardware table.

### Add a PSU

1. Implement the `PsuBackend` contract under `src/psu/<vendor_model>/` and keep its
   wire protocol isolated from platform USB code.
2. Decode USB replies into a complete `TelemetrySnapshot`; do not let the PMBus
   handler query the PSU directly.
3. Define polling cadence, reply validation, detach/recovery behavior, page/rail
   mapping, identity fields, and stale-data behavior.
4. Add a configure-time `PSU_USB_TO_PMBUS_PSU` selection and focused protocol,
   conversion, malformed-reply, and recovery tests.
5. Document verified commands and limitations in `doc/psu/<PSU>.md` and update the
   supported hardware table.

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
