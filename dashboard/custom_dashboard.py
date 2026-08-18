#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))

from profile_manager import ProfileManager, demo_live_data


def render_dashboard(profile_path: str, brand_override: str = "") -> str:
    manager = ProfileManager(profile_path)
    state = manager.build_dashboard_state(demo_live_data(), brand_override=brand_override or None)
    theme = state.theme
    lines = [
        "=" * 54,
        f"{state.vehicle_name}",
        f"Brand Logo: {state.logo_path}",
        f"Accent: {theme['accent']}   Warning: {theme['warning']}   Critical: {theme['critical']}",
        "=" * 54,
    ]
    for gauge in state.gauges:
        value = gauge.value
        if value is None:
            value_text = "--"
        else:
            value_text = f"{value:.1f}"
        lines.append(f"{gauge.label:<16} {value_text:>8} {gauge.unit}   [{gauge.color}]")
    lines.append("=" * 54)
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description="Render a customizable Subaru WRX dashboard state.")
    parser.add_argument("--profile", default="config/subaru_wrx_profile.json", help="Path to profile JSON")
    parser.add_argument("--brand", default="", help="Override the detected brand logo")
    args = parser.parse_args()

    profile_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), args.profile)
    print(render_dashboard(profile_path, args.brand))


if __name__ == "__main__":
    main()
