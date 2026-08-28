# PMBusTestHost

`PMBusTestHost.ino` is a generic USB-serial-to-I2C transaction bridge. PMBus command knowledge, PEC, decoding, verification, and reporting stay in Python. The sketch uses only Arduino `Serial`/the core's `SERIAL_PORT_USBVIRTUAL` alias and `Wire`, fixed buffers, and no dynamic `String`.

## Wiring

| Arduino | Feather RP2040 USB Host |
|---|---|
| SDA | GPIO2 / SDA |
| SCL | GPIO3 / SCL |
| GND | GND |

Use external 2.2-4.7 kOhm pull-ups from SDA and SCL to **3.3 V**. Do not pull the bus to 5 V. On the Due, use the pins labelled SDA and SCL; its I/O is 3.3 V.

The current live board is an Arduino Due Native USB Port on `COM8`:

```powershell
arduino-cli compile --upload --port COM8 --fqbn arduino:sam:arduino_due_x tools/PMBusTestHost
```

Keep compile and upload in the same command. A standalone `arduino-cli upload` can reuse the last artifact compiled for a different FQBN.

Portable compile checks:

```powershell
arduino-cli compile --fqbn arduino:avr:uno tools/PMBusTestHost
arduino-cli compile --fqbn arduino:sam:arduino_due_x tools/PMBusTestHost
arduino-cli compile --fqbn arduino:samd:arduino_zero_native tools/PMBusTestHost
```

## Serial protocol

The port is 115200 baud, 8-N-1. Commands and responses are ASCII lines with a caller-supplied decimal sequence number.

- `I <sequence>` returns bridge version and maximum I2C write/read sizes.
- `X <sequence> <address7> <RS|STOP> <read_length> <write_hex|->` executes one I2C transaction.
- `RS` holds the bus between a non-empty write and read; `STOP` ends the write first.
- `OK <sequence> <read_hex|->` reports success.
- `ERR <sequence> <PARSE|RANGE|NACK_ADDR|NACK_DATA|I2C|SHORT_READ> <detail>` reports failure.

Examples:

```text
I 1
X 2 0x58 RS 3 88
X 3 0x58 STOP 0 0001A4
```

The maximum read and write payload is 32 bytes for compatibility with the smallest standard `Wire` buffers. Python must include PMBus PEC bytes in its requested length and outgoing payload.

From the repository root, create `.venv`, then use:

```powershell
./scripts/bootstrap-tests.ps1
./.venv/Scripts/python.exe -m tools.pmbus_test scan --port COM8
./.venv/Scripts/python.exe -m tools.pmbus_test verify --port COM8 --address 0x58
./.venv/Scripts/python.exe -m tools.pmbus_test reference --port COM8 --address 0x58
```

## Real-hardware reference test

The `reference` command is the regression baseline captured from the working
AX1600i, Feather, and GIGABYTE MZ73-LM0 BMC integration. It:

- verifies the Corsair identity, RP2040 serial, PEC capability, and 1600 W
  `MFR_POUT_MAX` value used during motherboard discovery;
- checks aggregate pages 0-2 against iCUE's `READ_POUT = READ_VOUT * READ_IOUT`
  behavior;
- replays the exact harmless MZ73 polling writes for `SMBALERT_MASK`,
  `PAGE_PLUS_WRITE`, and `D0 00`;
- reads native AX1600i input power from `READ_PIN` once per second for five
  cycles and confirms that the compatibility traffic never leaves CML set;
- records a derived output/input ratio as a non-gating diagnostic. Corsair's
  independently sampled rail and input sensors can disagree, so the ratio is
  never treated as an efficiency contract or used to synthesize `READ_PIN`.

Use `--cycles`, `--interval`, or `--alert-mask` only when intentionally testing
a different polling pattern. Results are written under ignored `test-results/`
as `pmbus-motherboard-reference-*.json`.

The 2026-08-27 live run on the installed PSU observed native input power of
397-401 W, clean zero CML/status values in all five cycles, and `0xFF` readback
for both captured alert masks. Aggregate output was intentionally allowed to
differ from input: the independently sampled Corsair values produced a
nonphysical diagnostic ratio during this run, confirming that it must not become
an efficiency assertion or an alternate input-power calculation.
