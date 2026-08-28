# Corsair AX1600i backend

This document records the verified USB protocol and telemetry behavior used by
the `CORSAIR_AX1600I` backend.

## Overview

The AX1600i exposes a Silicon Labs USBXpress/CP210x-style bridge with Corsair's
USB identity and a vendor-specific encoded register protocol. It is not a
standard USB CDC device despite its serial-bridge ancestry.

| Property | Value |
|---|---|
| USB VID:PID | `1b1c:1c11` |
| Interface | 0 |
| Bulk OUT endpoint | `0x02` |
| Bulk IN endpoint | `0x82` |
| CMake selection | `PSU_USB_TO_PMBUS_PSU=CORSAIR_AX1600I` |

The implementation uses a custom asynchronous TinyUSB host-class driver. All
buffers and state machines have fixed storage.

## Bridge initialization

USBXpress is enabled once per attachment with vendor control request `0x02`,
request type `0x40`, value `0x0002`, and index 0. The bridge is then initialized
with this decoded payload:

```text
11 02 64 00 00 00 00
```

Operational timeouts do not repeat vendor-control setup. They discard the current
sample and restart the local Corsair register sequence. Only ten seconds of
continuous operational failure may flush and reopen an attached bridge; any
successful operation clears that timer.

## Wire encoding

Each decoded byte is represented by two encoded bytes. The low nibble is emitted
first, followed by the high nibble, through this table:

```text
0:55 1:56 2:59 3:5A 4:65 5:66 6:69 7:6A
8:95 9:96 A:99 B:9A C:A5 D:A6 E:A9 F:AA
```

An encoded command byte precedes the payload and a zero byte terminates the
record. Replies are asymmetric: that terminator decodes as a trailing zero byte.
A terminator-only data record therefore decodes as an empty transient response
and is retried locally.

## Register reads

A register read is a three-exchange handshake using decoded messages:

```text
13 03 06 01 07 <length> <register>
12
08 07 <length>
```

The first exchange must return exactly `{0}` after decoding. The trigger exchange
must return exactly `{0, 0}`. A short, long, nonzero, malformed, or empty
acknowledgement is not register data and must not advance the transaction.

The three messages retain a 5 ms settling cadence. Complete register operations
are separated by 100 ms so the USB bridge is not continuously saturated.

## Verified registers

| Register | Meaning | Use |
|---:|---|---|
| `0x00` | Rail selector | Selects aggregate rail |
| `0x88` | Input voltage | PMBus `READ_VIN` |
| `0x89` | Input current | PMBus `READ_IIN` |
| `0x8B` | Rail output voltage | PMBus `READ_VOUT` |
| `0x8C` | Rail output current | PMBus `READ_IOUT` |
| `0x8D` | Temperature 2 | PMBus temperature telemetry |
| `0x8E` | Temperature 1 | PMBus temperature telemetry |
| `0x90` | Fan speed | PMBus fan telemetry |
| `0x96` | Native rail output power | Diagnostic only |
| `0x97` | Native input power | PMBus `READ_PIN` |
| `0x9A` | Product name | PMBus identity |
| `0xD2` | Uptime | Manufacturer telemetry |
| `0xE7` | Branch selector | Selects branch channel |
| `0xE8` | Branch current | Branch-page telemetry |
| `0xE9` | Branch power | Branch-page telemetry |
| `0xEA` | Branch OCP threshold | Manufacturer limit telemetry |
| `0xEE` | Native total DC output power | Diagnostic only |

Numeric values use the Corsair/PMBus LINEAR11 representation where applicable.
Static identity is read once per attachment; electrical telemetry is refreshed
through a slower round-robin walk.

VIN, IIN, and native PIN reads are interleaved through that walk. This keeps input
power close to iCUE's observed update cadence while the PMBus side continues to
serve cached snapshots.

## Power interpretation

Native input power is read directly from register `0x97`. A read-only scan of
`0x00`–`0xFF` did not identify a credible standalone efficiency register, so the
firmware never uses an efficiency curve to fabricate input power.

iCUE establishes the output-power reference behavior:

- aggregate rail power is `VOUT × IOUT`;
- total output power is the sum of the 12 V, 5 V, and 3.3 V products; and
- native `0x96` and `0xEE` readings are retained only for diagnostics.

This distinction matters because native `0x96` can report zero for a rail while
iCUE still displays a nonzero voltage/current product. Diagnostic efficiency, if
shown, is derived output divided by a contemporaneous `0x97` sample and is never
used to generate a PMBus reading.

## PMBus page mapping

| PMBus page | Source |
|---:|---|
| `0x00` | Aggregate 12 V rail |
| `0x01` | Aggregate 5 V rail |
| `0x02` | Aggregate 3.3 V rail |
| `0x10`–`0x1B` | Branch channels 0–11 |

The backend reports the rated 1600 W output through `MFR_POUT_MAX` in LINEAR11.
`MFR_SERIAL` comes from the MCU platform rather than the PSU because the bridge
does not provide a usable serial through the verified register interface.

`READ_EIN` is synthesized from successful native input-power samples using the
AC/DC server profile's Direct-format accumulator semantics: two-byte `Paccum`, an
8-bit rollover counter, and an aligned 24-bit sample count. It is not energy in
watt-hours.

## Failure handling

- An empty data record is retried locally.
- A malformed acknowledgement or ordinary transfer failure discards only the
  affected sample and restarts the current register sequence.
- The telemetry walk continues after ordinary failures instead of restarting from
  its first item.
- Detach and recovery preserve the last complete snapshot.
- PMBus invalidates live telemetry only after ten seconds without a successful
  USB operation, then returns zero readings with OFF status.
- USB loss never becomes an INPUT or sticky CML fault.

## Diagnostic register scan

The `rp2040-ax1600i-scan` preset enables a read-only scan of registers
`0x00`–`0xFF` using two-byte reads and emits `AXSCAN` log records.

Unknown addresses may return patterned values rather than errors. A scan value is
therefore evidence only; do not assign it a meaning without an independent trace,
reference implementation, or controlled physical experiment.

## Adding another Corsair PSU

Do not assume that another Corsair model shares this VID/PID, handshake, register
map, rail selection, or scaling. Start with a separate backend unless captures
prove that the protocol is identical. Follow the root [porting guide](../../README.md#add-a-psu)
and keep model-specific behavior out of the PMBus server.
