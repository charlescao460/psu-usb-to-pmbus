# BMC compatibility FAQ

This page collects PMBus-controller compatibility guidance and motherboard-specific
findings. It is intentionally separate from the core architecture: a BMC quirk
must not leak into the PSU USB protocol or create unsafe control behavior.

## Is this a certified PMBus server power supply?

No. The firmware implements the useful read-only monitoring portion of the PMBus
AC/DC server power-supply profile. It omits the physical SMBAlert wire and does
not implement PSU control. `SMBALERT_MASK` is maintained as local logical state so
controllers can complete discovery, but there is no alert pin.

## What should a BMC discover?

A compatible controller can read manufacturer, model, revision, the MCU-derived
serial, rated output power, status, and electrical telemetry. The default target
address is `0x58`; deployments may configure another address at build time.

Some BMCs treat `MFR_POUT_MAX` as inventory capacity. Returning an unsupported
fill such as `0xFFFF` can make a UI report a bogus 65535 W supply, so the AX1600i
backend reports its rated 1600 W in LINEAR11.

## Why are some writes accepted by read-only firmware?

Discovery stacks commonly write page state, alert masks, or manufacturer-specific
probe values before reading telemetry. Safe compatibility writes can be stored
locally or accepted as no-ops, but they never reach the USB-connected PSU.

The implementation accepts:

- `PAGE` and broadcast `PAGE=0xFF` without losing the last readable page;
- logical `SMBALERT_MASK` directly and through `PAGE_PLUS_WRITE`;
- status clear operations and command-only `CLEAR_FAULTS`;
- safe manufacturer-specific writes in the compatibility range; and
- the observed `D0 00` discovery transaction as an explicit no-op.

Fan commands and all other PSU-control writes remain rejected.

## Why does CML not remain set after malformed traffic?

Several motherboard discovery sequences send optional or vendor-specific probes.
A sticky CML bit can cause a controller to classify the supply as permanently
faulty even after subsequent valid reads.

Unsupported discovery commands and safe ignored writes do not set CML. A malformed
known transaction or bad PEC can produce a one-shot CML detail, consumed by the
first `STATUS_BYTE`, `STATUS_WORD`, or `STATUS_CML` read. This deliberate
compatibility behavior keeps diagnostics observable without poisoning later polls.

## What happens when USB telemetry stops?

The PMBus target does not disappear immediately. It preserves the last complete
snapshot for ten seconds of continuous USB failure. After that interval it keeps
answering requests with zero telemetry and OFF status. USB transport loss is not
reported as an AC input failure or CML fault.

This makes transient USB recovery independent from BMC discovery and avoids
blocking the I2C handler on a USB transaction.

## What does a vendor UI error such as `0x8001` mean?

Values such as `0x8001` are BMC/UI-specific sensor states, not universal PMBus
status words. Check the actual I2C traffic before assigning a meaning. Useful
questions include:

1. Does the controller receive an ACK at the configured address?
2. Are block lengths and PEC bytes correct?
3. Does it read identity and `MFR_POUT_MAX` successfully?
4. Does it use repeated-start or process-call framing?
5. Is it expecting a physical SMBAlert signal?
6. Does its sensor map consume `READ_PIN`, or only display an internally derived
   system-power sensor?

The Arduino bridge and Python `verify` command can validate the target separately
from a motherboard. A logic analyzer can then show how the BMC sequence differs.

## Why can inventory work while system power remains zero?

Inventory and chassis-power reporting often use different BMC subsystems. A BMC
may successfully discover PMBus identity and poll `READ_PIN` while its web UI or
DCMI endpoint remains zero because no derived system-power sensor consumes those
readings.

Do not change standard PMBus encoding to compensate until a trace proves the raw
reading is wrong. First inspect the controller's active sensor map and compare its
raw PSU sensor with its aggregate system-power sensor.

## Gigabyte MZ73-LM0 case study

The following result is specific to **MZ73-LM0-000 BMC firmware 13.06.27**. It is
documented as a diagnostic example, not as a portable patch recipe.

### Observed behavior

- Cold-start discovery populated the Corsair/AX1600i inventory and read the
  1600 W `MFR_POUT_MAX` value.
- The BMC polled valid native `READ_PIN` (`0x97`) values roughly once per second.
- Inventory and raw PSU monitoring worked, but DCMI, Redfish
  `PowerConsumedWatts`, and the web system-power page remained at zero.
- The polling sequence included valid-PEC writes for `SMBALERT_MASK`,
  `PAGE_PLUS_WRITE` wrapping that mask, and `D0 00` before reading `0x97`.

### Firmware finding

The active `MZ73-LM0-000.xml` devmap defines `PSU_POWER` producers at PMBus
addresses `0x58` and `0x59`. Both use command `0x97` and link 13. However, all six
MZ73 devmaps in that firmware image omit the derived `SYS_POWER` sensor.

Comparable Gigabyte devmaps that expose chassis power add:

- a `SUM` taker for link 13;
- a `DIVIDE` operation with divisor 25; and
- sensor `0xE9` named `SYS_POWER`.

`libgbt.so::Check_get_sys_power_no` searches the active devmap specifically for
`sdr="SYS_POWER"`. Without that entry, the DCMI and web paths have no aggregate
sensor even though both PMBus PSU readings are valid.

### Validation

The diagnosis was validated with Gigabyte's external model-patch mechanism. A
minimally changed, version-incremented devmap added the missing `SYS_POWER` block
without modifying the base BMC image or FRU data. After reboot, sensor `0xE9`, the
DCMI endpoint, and the web UI all reported meaningful readings in the observed
400–425 W range.

This was a motherboard devmap defect, not a Corsair-vendor restriction and not a
LINEAR11 incompatibility. No nonstandard workaround belongs in this firmware.

### Patch warning

Never install that model patch on a different board model or BMC firmware revision
without repeating the firmware analysis. Sensor-map layouts, scaling, validation,
and recovery endpoints can change. Keep patch tooling and binary analysis in a
separate model-specific project rather than this embedded firmware repository.

For the validated firmware, the identified rollback operation was
`GET /api/maintenance/clear_model_patch` followed by a BMC reboot. Treat that as
version-specific information, not a general Gigabyte API guarantee.
