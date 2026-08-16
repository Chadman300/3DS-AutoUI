#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from typing import Any, Dict

sys.path.insert(0, os.path.dirname(__file__))

from renderer import ThreeDSRenderer, build_default_settings


def load_settings(path: str):
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)

    settings = build_default_settings()
    settings.vehicle_name = data.get("vehicle_name", settings.vehicle_name)
    settings.accent_color = data.get("accent_color", settings.accent_color)
    settings.brand_logo = data.get("brand_logo", settings.brand_logo)
    if "theme" in data:
        settings.apply_theme(data["theme"])
    if "gauges" in data:
        settings.gauges = []
        for item in data["gauges"]:
            settings.gauges.append(
                type(settings.gauges[0],) if settings.gauges else type("G", (), {})
            )
    return settings


def sample_live_data() -> Dict[str, Any]:
    return {
        "rpm": 3180.0,
        "speed": 62.0,
        "coolant_temp": 88.0,
        "boost": 1.55,
        "battery_voltage": 13.9,
        "throttle_position": 38.0,
        "engine_load": 46.0,
        "fuel_level": 71.0,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="3DS-ready dashboard renderer")
    parser.add_argument("--settings", default="config/default_settings.json", help="Path to settings JSON")
    parser.add_argument("--demo", action="store_true", help="Run with demo data")
    args = parser.parse_args()

    settings_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), args.settings)
    settings = build_default_settings()
    if os.path.exists(settings_path):
        try:
            with open(settings_path, "r", encoding="utf-8") as handle:
                data = json.load(handle)
            settings.vehicle_name = data.get("vehicle_name", settings.vehicle_name)
            settings.accent_color = data.get("accent_color", settings.accent_color)
            settings.brand_logo = data.get("brand_logo", settings.brand_logo)
            settings.apply_theme(data.get("theme", {}))
            settings.gauges = []
            for item in data.get("gauges", []):
                from renderer import GaugeRenderConfig
                settings.gauges.append(
                    GaugeRenderConfig(
                        id=item["id"],
                        label=item["label"],
                        unit=item["unit"],
                        min=float(item["min"]),
                        max=float(item["max"]),
                        warning=float(item["warning"]),
                        critical=float(item["critical"]),
                        color=item["color"],
                        enabled=bool(item.get("enabled", True)),
                    )
                )
        except Exception:
            pass

    renderer = ThreeDSRenderer(settings)
    payload = sample_live_data() if args.demo else sample_live_data()

    for _ in range(2):
        print(renderer.render_screen(payload))
        time.sleep(0.2)


if __name__ == "__main__":
    main()
