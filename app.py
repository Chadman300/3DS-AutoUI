#!/usr/bin/env python3
"""Main entry point for the 3DS AutoUI project.

This app combines the OBD2 bridge, the WRX profile, and the dashboard renderer so the
project behaves like a single application instead of separate demo scripts.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Any, Dict, Optional

from bridge.obd_bridge import connect_to_adapter, demo_payload, fetch_gauges
from dashboard.custom_dashboard import render_dashboard
from dashboard.profile_manager import ProfileManager, demo_live_data
from dashboard.renderer import ThreeDSRenderer, build_default_settings


ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_PROFILE = os.path.join(ROOT_DIR, "config", "subaru_wrx_profile.json")


def build_dashboard_payload(config_path: str, demo: bool = False, host: str = "192.168.0.10", port: int = 35000) -> Dict[str, Any]:
    if demo:
        return demo_payload()

    try:
        sock = connect_to_adapter(host, port)
        payload = {"timestamp": 0, "vehicle": {"make": "Subaru", "model": "WRX", "year": 2010}, "gauges": fetch_gauges(sock)}
        sock.close()
        return payload
    except Exception as exc:  # pragma: no cover - CLI fallback
        print(f"Warning: could not connect to OBD2 adapter ({exc}). Falling back to demo data.", file=sys.stderr)
        return demo_payload()


def print_json(payload: Dict[str, Any]) -> None:
    print(json.dumps(payload, indent=2))


def print_dashboard_text(config_path: str, demo: bool = False, brand_override: Optional[str] = None) -> None:
    profile = ProfileManager(config_path)
    live = demo_live_data() if demo else demo_live_data()
    state = profile.build_dashboard_state(live, brand_override=brand_override or None)
    settings = build_default_settings()
    settings.vehicle_name = state.vehicle_name
    settings.brand_logo = state.make
    settings.accent_color = state.theme.get("accent", settings.accent_color)
    settings.apply_theme(state.theme)
    renderer = ThreeDSRenderer(settings)
    print(renderer.render_screen(live))


def main() -> None:
    parser = argparse.ArgumentParser(description="3DS AutoUI app")
    parser.add_argument("--profile", default=DEFAULT_PROFILE, help="Path to the vehicle profile JSON")
    parser.add_argument("--demo", action="store_true", help="Use demo data instead of connecting to a real OBD2 adapter")
    parser.add_argument("--host", default="192.168.0.10", help="OBD2 adapter IP address")
    parser.add_argument("--port", type=int, default=35000, help="OBD2 adapter port")
    parser.add_argument("--brand", default="", help="Manual brand override")
    parser.add_argument("--json-only", action="store_true", help="Print only the raw JSON payload")
    args = parser.parse_args()

    payload = build_dashboard_payload(args.profile, demo=args.demo, host=args.host, port=args.port)

    if args.json_only:
        print_json(payload)
        return

    print("=== RAW PAYLOAD ===")
    print_json(payload)
    print("=== DASHBOARD ===")
    print_dashboard_text(args.profile, demo=args.demo, brand_override=args.brand)


if __name__ == "__main__":
    main()
