from __future__ import annotations

import argparse
import sys
import time
from collections.abc import Callable
from statistics import fmean

from .bridge import Bridge, BridgeError
from .commands import Command, PecError, PmbusClient, Reading, output_power_matches_product
from .report import VerificationReport


def integer(text: str) -> int:
    return int(text, 0)


def print_reading(reading: Reading) -> None:
    value = f"{reading.value:.3f}" if isinstance(reading.value, float) else str(reading.value)
    print(f"{reading.command.name:24} {value:>12} {reading.unit}")


def scan(args: argparse.Namespace) -> int:
    with Bridge(args.port, timeout=args.timeout) as bridge:
        print(bridge.info())
        found = []
        addresses = [args.address] if args.address is not None else range(0x03, 0x78)
        for address in addresses:
            try:
                capability = PmbusClient(bridge, address).read_byte(Command.CAPABILITY)
                found.append(address)
                print(f"0x{address:02X}: CAPABILITY=0x{capability:02X}")
            except (BridgeError, PecError):
                pass
        return 0 if found else 1


def monitor(args: argparse.Namespace) -> int:
    with Bridge(args.port, timeout=args.timeout) as bridge:
        print(bridge.info())
        client = PmbusClient(bridge, args.address)
        try:
            while True:
                print(time.strftime("\n%Y-%m-%d %H:%M:%S"), f"page=0x{args.page:02X}")
                for reading in client.telemetry(args.page):
                    print_reading(reading)
                time.sleep(args.interval)
        except KeyboardInterrupt:
            return 0


def verify(args: argparse.Namespace) -> int:
    report = VerificationReport(args.port, args.address)
    with Bridge(args.port, timeout=args.timeout) as bridge:
        info = bridge.info()
        report.add("bridge-info", info.max_read >= 32 and info.repeated_start, value=info)
        client = PmbusClient(bridge, args.address)

        def check(name: str, operation: Callable[[], object], predicate: Callable[[object], bool] = bool) -> None:
            try:
                value = operation()
                report.add(name, predicate(value), value=value)
            except Exception as exc:  # hardware verification must record every failure
                report.add(name, False, f"{type(exc).__name__}: {exc}")

        check("capability-pec", lambda: client.read_byte(Command.CAPABILITY),
              lambda value: bool(int(value) & 0x80))
        check("pmbus-revision", lambda: client.read_byte(Command.PMBUS_REVISION),
              lambda value: int(value) == 0x22)
        check("application-profile", lambda: client.read_byte(Command.APP_PROFILE_SUPPORT),
              lambda value: int(value) == 0x04)
        check("query-read-vin", lambda: client.query(Command.READ_VIN),
              lambda value: bool(int(value) & 0x80))
        check("coefficients", lambda: client.coefficients(Command.READ_EIN),
              lambda value: len(value) == 5)  # type: ignore[arg-type]
        check("read-ein", client.read_ein,
              lambda value: value.power_accumulator <= 0x7FFF and  # type: ignore[attr-defined]
              value.sample_count <= 0xFFFFFF)  # type: ignore[attr-defined]
        check("page-plus-read", lambda: client.page_plus_read(0, Command.STATUS_WORD, 2),
              lambda value: len(value) == 2)  # type: ignore[arg-type]
        check("manufacturer", lambda: client.read_text(Command.MFR_ID).value,
              lambda value: "corsair" in str(value).lower())
        check("model", lambda: client.read_text(Command.MFR_MODEL).value,
              lambda value: bool(str(value)))
        check("serial", lambda: client.read_text(Command.MFR_SERIAL).value,
              lambda value: len(str(value)) == 16 and all(character in "0123456789ABCDEF"
                                                          for character in str(value)))
        check("rated-output-capacity", lambda: client.read_linear11(Command.MFR_POUT_MAX, "W"),
              lambda value: abs(float(value.value) - 1600.0) <= 1.0)  # type: ignore[attr-defined]

        def mz73_compatibility_cycle() -> dict[str, int]:
            client.mz73_compatibility_writes()
            fan_mask = client.read_alert_mask(Command.STATUS_FANS_1_2)
            input_mask = client.read_alert_mask(Command.STATUS_INPUT)
            status_word = client.read_word(Command.STATUS_WORD)
            status_cml = client.read_byte(Command.STATUS_CML)
            return {
                "fan_mask": fan_mask,
                "input_mask": input_mask,
                "status_word": status_word,
                "status_cml": status_cml,
            }

        check("mz73-compatibility-cycle", mz73_compatibility_cycle,
              lambda value: value["fan_mask"] == 0xFF and  # type: ignore[index]
              value["input_mask"] == 0xFF and  # type: ignore[index]
              not (value["status_word"] & 0x0002) and  # type: ignore[index]
              value["status_cml"] == 0)  # type: ignore[index]

        for page in (0x00, 0x01, 0x02, *range(0x10, 0x1C)):
            check(f"page-{page:02x}", lambda page=page: client.telemetry(page),
                  lambda value, page=page: len(value) == 9 and
                  (page > 0x02 or output_power_matches_product(value)))  # type: ignore[arg-type]

        for command in (Command.STATUS_BYTE, Command.STATUS_WORD, Command.STATUS_INPUT,
                        Command.STATUS_IOUT, Command.STATUS_TEMPERATURE, Command.STATUS_CML,
                        Command.STATUS_FANS_1_2):
            reader = client.read_word if command == Command.STATUS_WORD else client.read_byte
            check(command.name.lower(), lambda command=command, reader=reader: reader(command),
                  lambda _value: True)

        # Control writes are ignored locally. The compatibility policy also
        # requires that they cannot strand a motherboard in sticky CML.
        try:
            client.write_data(Command.FAN_COMMAND_1, b"\x00\x00")
            status_cml = client.read_byte(Command.STATUS_CML)
            report.add("fan-write-safely-ignored", status_cml == 0, value=status_cml)
        except BridgeError as exc:
            report.add("fan-write-safely-ignored", exc.code in {"NACK_DATA", "I2C"}, detail=str(exc))

        # Invalid PEC must not alter PAGE. This write deliberately corrupts the PEC.
        before = client.read_byte(Command.PAGE)
        body = bytes((Command.PAGE, 0x01, 0x00))
        try:
            bridge.transact(args.address, body, 0)
        except BridgeError:
            pass
        after = client.read_byte(Command.PAGE)
        report.add("invalid-pec-rejected", before == after, value={"before": before, "after": after})

    path = report.write()
    for item in report.checks:
        print(f"{'PASS' if item.passed else 'FAIL'} {item.name}: {item.detail or item.value}")
    print(f"Report: {path}")
    return 0 if report.passed else 1


def reference(args: argparse.Namespace) -> int:
    """Replay the live MZ73 poll pattern and check the iCUE power model."""
    report = VerificationReport(args.port, args.address, kind="motherboard-reference")
    with Bridge(args.port, timeout=args.timeout) as bridge:
        info = bridge.info()
        report.add("bridge-info",
                   info.max_write >= 32 and info.max_read >= 32 and
                   info.repeated_start and info.i2c_hz == 100000,
                   value=info)
        client = PmbusClient(bridge, args.address)

        def check(name: str, operation: Callable[[], object],
                  predicate: Callable[[object], bool] = bool) -> object | None:
            try:
                value = operation()
                report.add(name, predicate(value), value=value)
                return value
            except Exception as exc:  # live reference runs must retain every failure
                report.add(name, False, f"{type(exc).__name__}: {exc}")
                return None

        check("capability-pec", lambda: client.read_byte(Command.CAPABILITY),
              lambda value: bool(int(value) & 0x80))
        check("manufacturer", lambda: client.read_text(Command.MFR_ID).value,
              lambda value: "corsair" in str(value).lower())
        check("model", lambda: client.read_text(Command.MFR_MODEL).value,
              lambda value: "ax1600" in str(value).lower())
        check("serial", lambda: client.read_text(Command.MFR_SERIAL).value,
              lambda value: len(str(value)) == 16 and all(character in "0123456789ABCDEF"
                                                          for character in str(value)))
        check("rated-output-capacity", lambda: client.read_linear11(Command.MFR_POUT_MAX, "W"),
              lambda value: abs(float(value.value) - 1600.0) <= 1.0)  # type: ignore[attr-defined]

        rail_powers: list[float] = []
        for page in (0x00, 0x01, 0x02):
            readings = check(f"icue-page-{page:02x}",
                             lambda page=page: client.telemetry(page),
                             lambda value: len(value) == 9 and  # type: ignore[arg-type]
                             output_power_matches_product(value))  # type: ignore[arg-type]
            if isinstance(readings, list):
                power = next((float(reading.value) for reading in readings
                              if reading.command == Command.READ_POUT), None)
                if power is not None:
                    rail_powers.append(power)

        samples: list[dict[str, float | int]] = []
        for cycle in range(args.cycles):
            try:
                client.mz73_compatibility_writes(args.alert_mask)
                pin = client.read_linear11(Command.READ_PIN, "W")
                status_word = client.read_word(Command.STATUS_WORD)
                status_cml = client.read_byte(Command.STATUS_CML)
                sample = {
                    "cycle": cycle + 1,
                    "input_watts": float(pin.value),
                    "status_word": status_word,
                    "status_cml": status_cml,
                }
                samples.append(sample)
                report.add(
                    f"mz73-poll-{cycle + 1}",
                    0.0 < float(pin.value) <= 2000.0 and
                    not (status_word & 0x0002) and status_cml == 0,
                    value=sample,
                )
            except Exception as exc:
                report.add(f"mz73-poll-{cycle + 1}", False,
                           f"{type(exc).__name__}: {exc}")
            if cycle + 1 < args.cycles:
                time.sleep(args.interval)

        check("fan-alert-mask-readback",
              lambda: client.read_alert_mask(Command.STATUS_FANS_1_2),
              lambda value: int(value) == args.alert_mask)
        check("input-alert-mask-readback",
              lambda: client.read_alert_mask(Command.STATUS_INPUT),
              lambda value: int(value) == args.alert_mask)

        input_samples = [float(sample["input_watts"]) for sample in samples]
        if len(rail_powers) == 3 and input_samples:
            total_output = sum(rail_powers)
            average_input = fmean(input_samples)
            efficiency = total_output / average_input if average_input > 0 else 0.0
            report.add("icue-total-output", total_output > 0,
                       value={"rail_watts": rail_powers, "total_watts": total_output})
            report.add("derived-efficiency-diagnostic",
                       total_output > 0 and average_input > 0,
                       value={"output_watts": total_output,
                              "input_watts": average_input,
                              "ratio": efficiency},
                       detail="observational only; never used as a pass/fail efficiency "
                       "contract or to fabricate READ_PIN")
        else:
            report.add("icue-total-output", False,
                       detail="could not collect three rail powers and an input sample")

    path = report.write()
    for item in report.checks:
        print(f"{'PASS' if item.passed else 'FAIL'} {item.name}: {item.detail or item.value}")
    print(f"Report: {path}")
    return 0 if report.passed else 1


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description="PMBus test host over the generic Arduino bridge")
    subparsers = root.add_subparsers(dest="command", required=True)
    for name, function in (("scan", scan), ("monitor", monitor), ("verify", verify),
                           ("reference", reference)):
        command = subparsers.add_parser(name)
        command.set_defaults(function=function)
        command.add_argument("--port", default="COM8")
        command.add_argument("--address", type=integer, default=None if name == "scan" else 0x58)
        command.add_argument("--timeout", type=float, default=1.0)
        if name == "monitor":
            command.add_argument("--page", type=integer, default=0)
            command.add_argument("--interval", type=float, default=1.0)
        if name == "reference":
            command.add_argument("--cycles", type=int, default=5)
            command.add_argument("--interval", type=float, default=1.0)
            command.add_argument("--alert-mask", type=integer, default=0xFF)
    return root


def main() -> int:
    args = parser().parse_args()
    try:
        return int(args.function(args))
    except (BridgeError, PecError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
