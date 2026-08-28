from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


@dataclass
class Check:
    name: str
    passed: bool
    detail: str = ""
    value: Any = None


@dataclass
class VerificationReport:
    port: str
    address: int
    kind: str = "verify"
    started_at: str = field(default_factory=lambda: datetime.now(timezone.utc).isoformat())
    checks: list[Check] = field(default_factory=list)

    @property
    def passed(self) -> bool:
        return all(check.passed for check in self.checks)

    def add(self, name: str, passed: bool, detail: str = "", value: Any = None) -> None:
        self.checks.append(Check(name, passed, detail, value))

    def write(self, directory: Path = Path("test-results")) -> Path:
        directory.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        path = directory / f"pmbus-{self.kind}-{stamp}.json"
        payload = asdict(self)
        payload["passed"] = self.passed
        path.write_text(json.dumps(payload, indent=2, default=str) + "\n", encoding="utf-8")
        return path
