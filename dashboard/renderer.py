from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional


@dataclass
class ThemeConfig:
    background: str = "#0B1220"
    panel: str = "#111827"
    foreground: str = "#E5E7EB"
    accent: str = "#FF5A36"
    warning: str = "#FBBF24"
    critical: str = "#EF4444"
    success: str = "#34D399"
    secondary: str = "#60A5FA"


@dataclass
class GaugeRenderConfig:
    id: str
    label: str
    unit: str
    min: float
    max: float
    warning: float
    critical: float
    color: str
    enabled: bool = True


@dataclass
class DashboardSettings:
    vehicle_name: str = "Subaru WRX 2010"
    accent_color: str = "#FF5A36"
    theme: ThemeConfig = field(default_factory=ThemeConfig)
    gauges: List[GaugeRenderConfig] = field(default_factory=list)
    brand_logo: str = "subaru"

    def apply_theme(self, theme: Optional[Dict[str, str]] = None) -> None:
        if theme:
            for key, value in theme.items():
                if hasattr(self.theme, key):
                    setattr(self.theme, key, value)
        self.theme.accent = self.accent_color


class ThreeDSRenderer:
    def __init__(self, settings: DashboardSettings):
        self.settings = settings

    def render_status_band(self, value: float, unit: str, label: str, warning: float, critical: float) -> str:
        if value is None:
            value_text = "--"
        else:
            value_text = f"{value:.1f}"

        color = self.settings.theme.accent
        if value is not None and value >= critical:
            color = self.settings.theme.critical
        elif value is not None and value >= warning:
            color = self.settings.theme.warning

        return f"[{color}] {label:<12} {value_text:>7} {unit}"

    def render_screen(self, live_data: Dict[str, Any]) -> str:
        lines = []
        lines.append("=" * 48)
        lines.append(f"{self.settings.vehicle_name:<30}")
        lines.append(f"Brand: {self.settings.brand_logo.upper():<20}")
        lines.append("-" * 48)

        for gauge in self.settings.gauges:
            if not gauge.enabled:
                continue
            value = live_data.get(gauge.id)
            lines.append(self.render_status_band(value, gauge.unit, gauge.label, gauge.warning, gauge.critical))

        lines.append("-" * 48)
        lines.append(f"Theme: {self.settings.theme.accent}")
        lines.append("=" * 48)
        return "\n".join(lines)


def build_default_settings() -> DashboardSettings:
    settings = DashboardSettings(
        vehicle_name="Subaru WRX 2010",
        accent_color="#FF5A36",
        theme=ThemeConfig(),
        brand_logo="subaru",
        gauges=[
            GaugeRenderConfig("rpm", "RPM", "RPM", 0, 8000, 6500, 7200, "#FF5A36"),
            GaugeRenderConfig("speed", "Speed", "MPH", 0, 140, 110, 130, "#60A5FA"),
            GaugeRenderConfig("coolant_temp", "Coolant", "C", 0, 130, 95, 110, "#FBBF24"),
            GaugeRenderConfig("boost", "Boost", "psi", -5, 25, 18, 22, "#34D399"),
            GaugeRenderConfig("battery_voltage", "Voltage", "V", 10, 15, 11.5, 10.5, "#A78BFA"),
            GaugeRenderConfig("throttle_position", "Throttle", "%", 0, 100, 85, 95, "#F472B6"),
            GaugeRenderConfig("engine_load", "Load", "%", 0, 100, 80, 95, "#22D3EE"),
            GaugeRenderConfig("fuel_level", "Fuel", "%", 0, 100, 15, 8, "#FCD34D"),
        ],
    )
    settings.apply_theme()
    return settings
