# PMBusTestHost

`PMBusTestHost.ino` is a generic USB-serial-to-I2C transaction bridge for testing
PMBus targets. PMBus command knowledge, PEC, value decoding, verification, and
reporting stay in the Python host tools.

The sketch uses only Arduino `Serial` (or the core's `SERIAL_PORT_USBVIRTUAL`
alias), `Wire`, fixed buffers, and no dynamic `String`. It is intended to compile
on AVR, SAM, and SAMD boards with a serial connection and I2C controller support.

## Wiring

| Arduino | PMBus target |
|---|---|
| SDA | Target SDA |
| SCL | Target SCL |
| GND | Target GND |

For the current Feather RP2040 implementation, target SDA is GPIO2 and target SCL
is GPIO3.

Use external 2.2–4.7 kOhm pull-ups from SDA and SCL to **3.3 V**. Do not pull the
bus to 5 V. Confirm that the selected Arduino board is safe at 3.3 V; the Arduino
Due uses 3.3 V I/O on its pins labelled SDA and SCL.

## Compile and upload

List connected boards and select the serial port reported for the intended board:

```powershell
arduino-cli board list
$BridgePort = "YOUR_SERIAL_PORT"
```

Compile and upload to an Arduino Due Native USB port:

```powershell
arduino-cli compile --upload --port $BridgePort --fqbn arduino:sam:arduino_due_x tools/PMBusTestHost
```

Keep compile and upload in the same command. A standalone `arduino-cli upload`
can reuse an artifact previously built for a different fully qualified board name.

Portable compile checks:

```powershell
arduino-cli compile --fqbn arduino:avr:uno tools/PMBusTestHost
arduino-cli compile --fqbn arduino:sam:arduino_due_x tools/PMBusTestHost
arduino-cli compile --fqbn arduino:samd:arduino_zero_native tools/PMBusTestHost
```

## Serial protocol

The port uses 115200 baud, 8-N-1. Commands and responses are ASCII lines with a
caller-supplied decimal sequence number.

- `I <sequence>` returns bridge version and maximum I2C write/read sizes.
- `X <sequence> <address7> <RS|STOP> <read_length> <write_hex|->` executes one
  I2C transaction.
- `RS` holds the bus between a non-empty write and read; `STOP` ends the write
  first.
- `OK <sequence> <read_hex|->` reports success.
- `ERR <sequence> <PARSE|RANGE|NACK_ADDR|NACK_DATA|I2C|SHORT_READ> <detail>`
  reports failure.

Examples:

```text
I 1
X 2 0x58 RS 3 88
X 3 0x58 STOP 0 0001A4
```

The maximum read and write payload is 32 bytes for compatibility with the
smallest standard `Wire` buffers. Python includes PMBus PEC bytes in the requested
read length and outgoing payload.

## Python test host

From the repository root, create the virtual environment and pass the bridge port
explicitly to each live command:

```powershell
./scripts/bootstrap-tests.ps1
$BridgePort = "YOUR_SERIAL_PORT"
./.venv/Scripts/python.exe -m tools.pmbus_test scan --port $BridgePort
./.venv/Scripts/python.exe -m tools.pmbus_test verify --port $BridgePort --address 0x58
./.venv/Scripts/python.exe -m tools.pmbus_test monitor --port $BridgePort --address 0x58
```

The CLI provides:

- `scan` to probe PMBus addresses through the bridge;
- `monitor` to print selected telemetry repeatedly;
- `verify` to check discovery, identity, supported pages, PEC, conversions, status,
  and rejected writes; and
- `reference` to replay the captured non-control motherboard traffic and validate
  the AX1600i/iCUE power relationship.

The `reference` command is a hardware regression test, not a general PMBus
conformance suite. It checks identity, the 1600 W `MFR_POUT_MAX`, aggregate
`READ_POUT = READ_VOUT × READ_IOUT`, native `READ_PIN`, and harmless local
compatibility writes. It also records output/input ratio as a non-gating diagnostic;
that ratio is never used to synthesize input power.

Use `--cycles`, `--interval`, or `--alert-mask` only when intentionally testing a
different polling pattern. Timestamped JSON results are written under the ignored
`test-results/` directory.

See the repository's [BMC compatibility FAQ](../../doc/FAQ_BMC.md) for the
motherboard sequence behind the reference test and the MZ73-LM0 case study.
