from __future__ import annotations

from typing import Optional


class BrandDetector:
    """Simple make/model detection helper for local brand assets."""

    @staticmethod
    def detect_brand(vin: Optional[str] = None, make: Optional[str] = None, model: Optional[str] = None) -> str:
        text = " ".join(part for part in [vin or "", make or "", model or ""] if part).lower()

        if any(token in text for token in ["subaru", "wrx", "impreza"]):
            return "subaru"
        if any(token in text for token in ["ford", "mustang", "focus", "f150"]):
            return "ford"
        if any(token in text for token in ["toyota", "corolla", "supra", "camry"]):
            return "toyota"
        if any(token in text for token in ["honda", "civic", "accord", "type r"]):
            return "honda"
        if any(token in text for token in ["bmw", "m3", "m5", "x5"]):
            return "bmw"

        return "generic"
