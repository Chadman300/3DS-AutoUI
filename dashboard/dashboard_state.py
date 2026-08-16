from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional


@dataclass
class GaugeState:
    id: str
    label: str
    unit: str
    value: Optional[float]
    min: float
    max: float
    warning: float
    critical: float
    color: str


@dataclass
class DashboardState:
    vehicle_name: str
    make: str
    model: str
    theme: Dict[str, str]
    logo_path: str
    gauges: List[GaugeState] = field(default_factory=list)
