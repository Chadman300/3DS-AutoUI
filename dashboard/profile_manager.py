from __future__ import annotations

import json
import os
import sys
from dataclasses import dataclass
from typing import Any, Dict, List, Optional

sys.path.insert(0, os.path.dirname(__file__))

from brand_detector import BrandDetector
from dashboard_state import DashboardState, GaugeState


@dataclass
class GaugeProfile:
    id: str
    label: str
    unit: str
    min: float
    max: float
    warning: float
    critical: float
    color: str


class ProfileManager:
    def __init__(self, profile_path: str):
        self.profile_path = profile_path
        with open(profile_path, "r", encoding="utf-8") as handle:
            self.profile = json.load(handle)

    def vehicle_name(self) -> str:
        vehicle = self.profile.get("vehicle", {})
        make = vehicle.get("make", "Vehicle")
        model = vehicle.get("model", "Model")
        year = vehicle.get("year", "")
        if year:
            return f"{make} {model} {year}"
        return f"{make} {model}"

    def theme(self, accent_override: Optional[str] = None) -> Dict[str, str]:
        theme = self.profile.get("theme", {})
        result = dict(theme)
        if accent_override:
            result["accent"] = accent_override
        return result

    def gauges(self) -> List[GaugeProfile]:
        items: List[GaugeProfile] = []
        for gauge in self.profile.get("gauges", []):
            items.append(
                GaugeProfile(
                    id=gauge["id"],
                    label=gauge["label"],
                    unit=gauge["unit"],
                    min=float(gauge["min"]),
                    max=float(gauge["max"]),
                    warning=float(gauge["warning"]),
                    critical=float(gauge["critical"]),
                    color=gauge["color"],
                )
            )
        return items

    def logo_path(self, brand_override: Optional[str] = None) -> str:
        vehicle = self.profile.get("vehicle", {})
        make = (brand_override or vehicle.get("make") or "generic").lower()
        base_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), "assets", "logos")
        candidate = os.path.join(base_dir, f"{make}.svg")
        if os.path.exists(candidate):
            return candidate
        return os.path.join(base_dir, "generic.svg")

    def build_dashboard_state(self, live_data: Dict[str, Any], brand_override: Optional[str] = None) -> DashboardState:
        vehicle = self.profile.get("vehicle", {})
        make = (brand_override or vehicle.get("make") or BrandDetector.detect_brand()).lower()
        model = vehicle.get("model", "Unknown")
        theme = self.theme()
        logo_path = self.logo_path(make)
        gauges: List[GaugeState] = []
        for gauge in self.gauges():
            gauges.append(
                GaugeState(
                    id=gauge.id,
                    label=gauge.label,
                    unit=gauge.unit,
                    value=live_data.get(gauge.id),
                    min=gauge.min,
                    max=gauge.max,
                    warning=gauge.warning,
                    critical=gauge.critical,
                    color=gauge.color,
                )
            )

        return DashboardState(
            vehicle_name=self.vehicle_name(),
            make=make,
            model=model,
            theme=theme,
            logo_path=logo_path,
            gauges=gauges,
        )


def demo_live_data() -> Dict[str, Any]:
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
