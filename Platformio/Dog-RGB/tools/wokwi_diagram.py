#!/usr/bin/env python3
"""Generate deterministic Wokwi instrumentation views from the canonical diagram."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def instrument(diagram: dict[str, Any], profile: str) -> dict[str, Any]:
    if profile not in {"full", "gnss"}:
        raise ValueError(f"unsupported capture profile: {profile}")

    logic = next((part for part in diagram["parts"] if part.get("id") == "logic"), None)
    if logic is None:
        raise ValueError("canonical diagram has no 'logic' analyzer")

    attrs = logic.setdefault("attrs", {})
    attrs["bufferSize"] = "1000000" if profile == "full" else "250000"
    if profile == "gnss":
        high_rate_led_channels = {"logic:D0", "logic:D1", "logic:D4"}
        diagram["connections"] = [
            connection
            for connection in diagram["connections"]
            if connection[1] not in high_rate_led_channels
        ]
    return diagram


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", choices=("full", "gnss"), required=True)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    diagram = json.loads(args.input.read_text(encoding="utf-8"))
    result = instrument(diagram, args.profile)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
