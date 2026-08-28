# PMBus command support

This document describes the PMBus command surface implemented by
`psu-usb-to-pmbus` for the current RP2040 and Corsair AX1600i configuration.

The status labels describe firmware behavior, not formal conformance or
certification. The project implements a read-only telemetry and discovery subset
of the [PMBus Application Profile for AC/DC Server Power Supplies Revision 1.2](https://pmbus.org/wp-content/uploads/2018/07/PMBus_App_Profile_ACDC_Server_Power_rev_1_2_20120416.pdf).

## Status legend

| Status | Meaning |
|---|---|
| Supported | The command and its intended local behavior are implemented. |
| Native | The value is obtained from a verified AX1600i USB register. |
| Derived | The value is calculated from native cached telemetry. |
| Synthesized | The response is generated locally because the PSU does not expose an equivalent value. |
| Local only | The command changes bridge/PMBus state or is accepted as a safe no-op; it never reaches the PSU. |
| Partial | The command responds, but one or more application-profile semantics are not implemented. |
| Rejected | The command is recognized but intentionally cannot control the PSU. |

Several rows combine labels, such as **Native / Supported** or
**Synthesized / Partial**.

## Command matrix

| Code | Command | Access | Profile relation | Status | Implementation notes |
|---:|---|---|---|---|---|
| `0x00` | `PAGE` | Read/write byte | Additional | Local only / Supported | Selects aggregate pages `0x00`–`0x02` or branch pages `0x10`–`0x1B`. Broadcast write `0xFF` is accepted without changing the last readable page. |
| `0x03` | `CLEAR_FAULTS` | Send byte | Required | Local only / Supported | Clears local one-shot CML state. It cannot clear faults inside the USB-connected PSU. |
| `0x05` | `PAGE_PLUS_WRITE` | Block write | Required | Local only / Partial | Supports valid local non-control writes. It does not provide independent persistent BMC/ME status banks as specified by the application profile. |
| `0x06` | `PAGE_PLUS_READ` | Write-block/read-block process call | Required | Partial | Performs a counted read on a specified supported page. Status data shares the bridge's single synthesized status state. |
| `0x19` | `CAPABILITY` | Read byte | Required | Supported | Returns `0x80`: PEC and 100 kHz are supported; physical SMBAlert is not. |
| `0x1A` | `QUERY` | Write-block/read-block process call | Required | Supported | Reports the implemented read/write capability and data format. The canonical counted request is supported; a legacy uncounted request is also accepted. |
| `0x1B` | `SMBALERT_MASK` | Process call/write word | Required | Local only / Partial | Masks are stored and read back locally, including through `PAGE_PLUS_WRITE`. There is no physical SMBAlert output. |
| `0x20` | `VOUT_MODE` | Read byte | Additional | Synthesized / Supported | Reports ULINEAR16 with exponent −12 for `READ_VOUT`. |
| `0x30` | `COEFFICIENTS` | Write-block/read-block process call | Required | Synthesized / Supported | Supports `READ_EIN` coefficients `m=1`, `b=0`, `R=0`. Canonical counted and legacy request forms are accepted. |
| `0x3A` | `FAN_CONFIG_1_2` | Read byte | Required | Synthesized / Supported | Returns `0x80`, indicating fan 1 present with duty-cycle command mode. Fan speed is still reported in RPM by `READ_FAN_SPEED_1`. |
| `0x3B` | `FAN_COMMAND_1` | Write word | Required | Rejected | Recognized but never applied or forwarded. The application profile's fan-speed-increase control is intentionally not implemented. |
| `0x78` | `STATUS_BYTE` | Read/write byte | Additional | Synthesized / Partial | Reports local OFF/CML summary state. A read consumes pending one-shot CML; writes clear local status. |
| `0x79` | `STATUS_WORD` | Read/write word | Required | Synthesized / Partial | Reports OFF after stale USB telemetry and one-shot CML. Native PSU fault bits and profile-persistent BMC/ME instances are not available. |
| `0x7B` | `STATUS_IOUT` | Read/write byte | Required | Synthesized / Partial | Currently reads zero and accepts local status-clear writes. Native overcurrent/overpower events are not exposed. |
| `0x7C` | `STATUS_INPUT` | Read/write byte | Required | Synthesized / Partial | Currently reads zero. USB telemetry loss is deliberately not reported as an AC-input fault. |
| `0x7D` | `STATUS_TEMPERATURE` | Read/write byte | Required | Synthesized / Partial | Currently reads zero and does not synthesize warning/fault bits from temperature thresholds. |
| `0x7E` | `STATUS_CML` | Read/write byte | Required | Local only / Partial | Reports malformed known traffic or PEC errors once, then clears on the first status observation. Unsupported discovery probes do not create sticky CML. |
| `0x81` | `STATUS_FANS_1_2` | Read/write byte | Required | Synthesized / Partial | Currently reads zero; native fan warning/fault events are not available. |
| `0x86` | `READ_EIN` | Block read | Required | Derived / Partial | Returns Direct-format `Paccum`, rollover count, and aligned 24-bit sample count. It accumulates successful USB `READ_PIN` samples rather than four-AC-cycle samples, so it is not fully profile-conformant. |
| `0x88` | `READ_VIN` | Read word | Additional | Native / Supported | AX1600i register `0x88`, LINEAR11. Returns zero after the USB stale-data grace period. |
| `0x89` | `READ_IIN` | Read word | Additional | Native / Supported | AX1600i register `0x89`, LINEAR11. Returns zero after the USB stale-data grace period. |
| `0x8B` | `READ_VOUT` | Read word | Additional | Native / Supported | AX1600i register `0x8B` for the selected rail; returned as ULINEAR16 with exponent −12. Branch pages reuse the 12 V rail voltage. |
| `0x8C` | `READ_IOUT` | Read word | Required | Native / Supported | AX1600i register `0x8C` on aggregate pages and branch register `0xE8` on branch pages. Page `0x00` is aggregate 12 V current. |
| `0x8D` | `READ_TEMPERATURE_1` | Read word | Required | Native / Partial | AX1600i temperature telemetry, LINEAR11. Sensor role and profile accuracy have not been independently calibrated as inlet temperature. |
| `0x8E` | `READ_TEMPERATURE_2` | Read word | Required | Native / Partial | AX1600i temperature telemetry, LINEAR11. Sensor role and profile accuracy have not been independently calibrated as the internal hot spot. |
| `0x90` | `READ_FAN_SPEED_1` | Read word | Required | Native / Supported | AX1600i register `0x90`, reported as LINEAR11 RPM. |
| `0x96` | `READ_POUT` | Read word | Additional | Derived / Supported | Aggregate pages use `VOUT × IOUT`, matching iCUE. Branch pages use native branch power. AX1600i native rail/total power registers remain diagnostic only. |
| `0x97` | `READ_PIN` | Read word | Required | Native / Partial | AX1600i register `0x97`, LINEAR11. No efficiency curve is used. Application-profile averaging and accuracy have not been independently certified. |
| `0x98` | `PMBUS_REVISION` | Read byte | Required | Synthesized / Supported | Returns `0x22`, identifying the PMBus 1.2 command set. |
| `0x99` | `MFR_ID` | Block read | Additional | Synthesized / Supported | Returns `Corsair`. |
| `0x9A` | `MFR_MODEL` | Block read | Additional | Native / Supported | Read from AX1600i register `0x9A` once per USB attachment. |
| `0x9B` | `MFR_REVISION` | Block read | Additional | Synthesized / Supported | Returns the bridge revision marker `USB-PMBus`. |
| `0x9E` | `MFR_SERIAL` | Block read | Additional | Synthesized / Supported | Returns the MCU platform's stable uppercase 16-digit identifier. |
| `0x9F` | `APP_PROFILE_SUPPORT` | Read byte | Required | Synthesized / Partial | Returns `0x04` for BMC discovery compatibility. This must not be interpreted as a claim of complete Rev. 1.2 profile conformance. |
| `0xA6` | `MFR_IOUT_MAX` | Read word | Required | Native / Partial | Returns the selected page's cached OCP limit. AX1600i branch limits are available; aggregate-rail limits currently return zero. |
| `0xA7` | `MFR_POUT_MAX` | Read word | Additional | Synthesized / Supported | Returns the AX1600i rated 1600 W output in LINEAR11 for inventory discovery. |
| `0xC0` | `MFR_MAX_TEMP_1` | Read word | Required | Synthesized / Partial | Returns a fixed 105 °C LINEAR11 value rather than a verified AX1600i inlet-warning threshold. |
| `0xC1` | `MFR_MAX_TEMP_2` | Read word | Required | Synthesized / Partial | Returns a fixed 105 °C LINEAR11 value rather than a verified AX1600i hot-spot-warning threshold. |
| `0xD0` | `MFR_BRIDGE_STATUS` | Read byte/write | Manufacturer extension | Local only / Supported | Read reports bridge state: fresh, stale, or disconnected. Compatibility writes such as `D0 00` are accepted as no-ops. |
| `0xD2` | `MFR_UPTIME` | Read word | Manufacturer extension | Native / Supported | Returns the low 16 bits of AX1600i uptime register `0xD2`. |
| `0xEA` | `MFR_BRANCH_OCP` | Read word | Manufacturer extension | Native / Supported | Returns AX1600i branch OCP register `0xEA` on pages `0x10`–`0x1B`. |

## Page behavior

| Page | Telemetry source |
|---:|---|
| `0x00` | Aggregate 12 V rail |
| `0x01` | Aggregate 5 V rail |
| `0x02` | Aggregate 3.3 V rail |
| `0x10`–`0x1B` | AX1600i branch channels 0–11 |

These pages are telemetry selections. They do not currently implement the
application profile's independent page `0x00` BMC and page `0x01` management-engine
status latches.

## General transaction behavior

- Supported reads are served entirely from the latest atomic telemetry snapshot.
- Read responses include PEC. The firmware reports PEC and 100 kHz support through
  `CAPABILITY`.
- Known writes accept PEC. For motherboard compatibility, an unprotected write is
  also accepted when its payload has an unambiguous command-specific shape.
- Manufacturer-specific writes from `0xC4` through `0xFD` are accepted as local
  no-ops unless they have an explicitly implemented local behavior. They never
  reach the PSU.
- Unsupported reads return `0xFF` fill bytes. Unsupported writes outside the safe
  manufacturer-specific range are ignored.
- USB data remains readable for a ten-second grace period after the last successful
  core update. After that, telemetry reads return zero and `STATUS_WORD` asserts
  OFF only.
- Fan control and every other PSU-control write are intentionally unavailable.

## Application-profile assessment

The command surface covers most Rev. 1.2 telemetry and discovery commands, but
the implementation is not fully profile-conformant. Known gaps beyond physical
SMBAlert include:

- rejected `FAN_COMMAND_1` control;
- non-persistent, compatibility-oriented status and CML behavior;
- no independent BMC/management-engine status banks;
- USB-rate rather than four-AC-cycle `READ_EIN` samples;
- build-time rather than hardware-strapped PMBus addressing; and
- unverified profile electrical, averaging, and measurement-accuracy requirements.

The appropriate description is **a read-only telemetry and discovery
subset of the Rev. 1.2 AC/DC server power-supply application profile**
