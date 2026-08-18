#!/usr/bin/env python3
"""Simple dashboard renderer for 3DS AutoUI data.

This intentionally prints a text preview of the gauge payload so the project can be tested
without a real 3DS build environment.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Any, Dict


def load_json(path: str) -> Dict[str, Any]:
    if not os.path.exists(path):
        raise FileNotFoundError(path)
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def format_value(label: str, value: Any, unit: str) -> str:
    if value is None:
        return f"{label:>12}: -- {unit}"
    return f"{label:>12}: {value:>6.1f} {unit}"


def render_dashboard(data: Dict[str, Any]) -> str:
    vehicle = data.get("vehicle", {})
    gauges = data.get("gauges", {})

    lines = [
        "=" * 42,
        f"{vehicle.get('make', 'Vehicle')} {vehicle.get('model', 'Model')} {vehicle.get('year', '')}",
        "=" * 42,
        format_value("RPM", gauges.get("rpm"), "RPM"),
        format_value("Speed", gauges.get("speed"), "MPH"),
        format_value("Coolant", gauges.get("coolant_temp"), "C"),
        format_value("Boost", gauges.get("boost"), "psi"),
        format_value("Throttle", gauges.get("throttle_position"), "%"),
        format_value("Load", gauges.get("engine_load"), "%"),
        format_value("Fuel", gauges.get("fuel_level"), "%"),
        format_value("Voltage", gauges.get("battery_voltage"), "V"),
        "=" * 42,
    ]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description="Render a text dashboard preview")
    parser.add_argument("--demo", action="store_true", help="Use sample demo data")
    parser.add_argument("--json", type=str, default="", help="Path to a JSON payload file")
    args = parser.parse_args()

    if args.demo:
        sample = {
            "vehicle": {"make": "Subaru", "model": "WRX", "year": 2010},
            "gauges": {
                "rpm": 3180.0,
                "speed": 62.0,
                "coolant_temp": 88.0,
                "boost": 1.55,
                "throttle_position": 38.0,
                "engine_load": 46.0,
                "fuel_level": 71.0,
                "battery_voltage": 13.9,
            },
        }
        print(render_dashboard(sample))
        return

    if not args.json:
        print("No JSON input. Use --demo or --json path/to/data.json")
        raise SystemExit(1)

    try:
        payload = load_json(args.json)
        print(render_dashboard(payload))
    except (FileNotFoundError, json.JSONDecodeError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
