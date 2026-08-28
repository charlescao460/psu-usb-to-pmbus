#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ -x "$project_root/.venv/bin/python" ]] || python3 -m venv "$project_root/.venv"
"$project_root/.venv/bin/python" -m pip install --upgrade pip
"$project_root/.venv/bin/python" -m pip install -r "$project_root/requirements-test.txt"

