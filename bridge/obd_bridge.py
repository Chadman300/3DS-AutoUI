#!/usr/bin/env python3
"""Starter OBD2 bridge for a Vgate iCar Pro-compatible Wi‑Fi adapter.

This module provides:
- ELM327 Wi‑Fi session setup
- vendor-neutral PID polling
- conversion to normalized gauge values
- a JSON payload suitable for a 3DS dashboard client

The design keeps OBD2 logic away from the 3DS UI layer.
"""

from __future__ import annotations

import argparse
import json
import socket
import time
from typing import Dict, Optional, Tuple


DEFAULT_HOST = "192.168.0.10"
DEFAULT_PORT = 35000
DEFAULT_TIMEOUT = 2.0


def clean_response(raw: bytes) -> str:
    text = raw.decode("utf-8", errors="ignore")
    text = text.replace("\r", "")
    text = text.replace("\n", "")
    text = text.replace("\x00", "")
    text = text.strip()
    return text


def send_elm_command(sock: socket.socket, command: str, timeout: float = DEFAULT_TIMEOUT) -> str:
    """Send an ELM327 command and return its response without the prompt."""
    sock.settimeout(timeout)
    sock.sendall((command + "\r").encode("utf-8"))

    buffer = ""
    deadline = time.time() + timeout

    while time.time() < deadline:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            break

        if not chunk:
            break

        buffer += clean_response(chunk)
        if ">" in buffer:
            break

    if buffer.startswith("NO DATA"):
        return "NO DATA"

    # Remove trailing prompt and noise
    buffer = buffer.replace(">", "")
    buffer = buffer.replace("OK", "")
    buffer = buffer.strip()
    return buffer


def init_elm(sock: socket.socket) -> None:
    """Initialize a standard ELM327 session."""
    for cmd in ["ATZ", "ATE0", "ATL0", "ATS0", "ATH0"]:
        response = send_elm_command(sock, cmd)
        if response == "":
            continue


def decode_hex_value(hex_value: str) -> Optional[int]:
    if not hex_value or len(hex_value) < 2:
        return None
    try:
        return int(hex_value, 16)
    except ValueError:
        return None


def parse_pid_010c(response: str) -> Optional[float]:
    # RPM: 0x010C = A*256 + B / 4
    if not response or len(response) < 4:
        return None
    parts = response.split()
    if len(parts) < 2:
        return None
    payload = parts[-1]
    if len(payload) < 4:
        return None
    a = decode_hex_value(payload[0:2])
    b = decode_hex_value(payload[2:4])
    if a is None or b is None:
        return None
    return ((a * 256) + b) / 4.0


def parse_pid_010d(response: str) -> Optional[float]:
    # Speed: A
    if not response or len(response) < 2:
        return None
    payload = response.split()[-1]
    if len(payload) < 2:
        return None
    value = decode_hex_value(payload[0:2])
    if value is None:
        return None
    return float(value)


def parse_pid_0105(response: str) -> Optional[float]:
    # Coolant temp: A - 40
    if not response or len(response) < 2:
        return None
    payload = response.split()[-1]
    if len(payload) < 2:
        return None
    value = decode_hex_value(payload[0:2])
    if value is None:
        return None
    return float(value - 40)


def parse_pid_010f(response: str) -> Optional[float]:
    # Intake air temp: A - 40
    if not response or len(response) < 2:
        return None
    payload = response.split()[-1]
    if len(payload) < 2:
        return None
    value = decode_hex_value(payload[0:2])
    if value is None:
        return None
    return float(value - 40)


def parse_pid_010b(response: str) -> Optional[float]:
    # MAP: A kPa
    if not response or len(response) < 2:
        return None
    payload = response.split()[-1]
    if len(payload) < 2:
        return None
    value = decode_hex_value(payload[0:2])
    if value is None:
        return None
    return float(value)


def parse_pid_0111(response: str) -> Optional[float]:
    # Throttle position: A * 100 / 255
    if not response or len(response) < 2:
        return None
    payload = response.split()[-1]
    if len(payload) < 2:
        return None
    value = decode_hex_value(payload[0:2])
    if value is None:
        return None
    return (value * 100.0) / 255.0


def parse_pid_0104(response: str) -> Optional[float]:
    # Engine load: A * 100 / 255
    if not response or len(response) < 2:
        return None
    payload = response.split()[-1]
    if len(payload) < 2:
        return None
    value = decode_hex_value(payload[0:2])
    if value is None:
        return None
    return (value * 100.0) / 255.0


def parse_pid_012f(response: str) -> Optional[float]:
    # Fuel level: A * 100 / 255
    if not response or len(response) < 2:
        return None
    payload = response.split()[-1]
    if len(payload) < 2:
        return None
    value = decode_hex_value(payload[0:2])
    if value is None:
        return None
    return (value * 100.0) / 255.0


def parse_pid_0142(response: str) -> Optional[float]:
    # Battery voltage: ((A * 256) + B) / 1000
    if not response or len(response) < 4:
        return None
    payload = response.split()[-1]
    if len(payload) < 4:
        return None
    a = decode_hex_value(payload[0:2])
    b = decode_hex_value(payload[2:4])
    if a is None or b is None:
        return None
    return ((a * 256) + b) / 1000.0


def map_to_boost_psi(map_kpa: Optional[float]) -> Optional[float]:
    if map_kpa is None:
        return None
    # Standard atmospheric pressure ~101.3 kPa => 0 psi at sea level under no boost.
    psi = (map_kpa - 101.3) / 6.89476
    if psi < 0:
        return 0.0
    return round(psi, 2)


def elm_query(sock: socket.socket, pid: str) -> Optional[str]:
    command = f"01{pid}"
    # Most OBD2 adapters accept 010C-style requests in ELM327 mode.
    response = send_elm_command(sock, command)
    if response in {"NO DATA", "", "ERROR"}:
        return None
    return response


def fetch_gauges(sock: socket.socket) -> Dict[str, Optional[float]]:
    queries = {
        "rpm": ("0C", parse_pid_010c),
        "speed": ("0D", parse_pid_010d),
        "coolant_temp": ("05", parse_pid_0105),
        "intake_temp": ("0F", parse_pid_010f),
        "map_kpa": ("0B", parse_pid_010b),
        "throttle_position": ("11", parse_pid_0111),
        "engine_load": ("04", parse_pid_0104),
        "fuel_level": ("2F", parse_pid_012f),
        "battery_voltage": ("42", parse_pid_0142),
    }

    values: Dict[str, Optional[float]] = {}
    for key, (pid, parser) in queries.items():
        raw = elm_query(sock, pid)
        if raw is None:
            values[key] = None
            continue
        values[key] = parser(raw)

    boost = map_to_boost_psi(values.get("map_kpa"))
    values["boost"] = boost
    return values


def make_payload() -> Dict[str, object]:
    payload = {
        "timestamp": int(time.time() * 1000),
        "vehicle": {
            "make": "Subaru",
            "model": "WRX",
            "year": 2010,
            "source": "OBD2 bridge",
        },
        "gauges": {
            "rpm": None,
            "speed": None,
            "coolant_temp": None,
            "intake_temp": None,
            "map_kpa": None,
            "boost": None,
            "throttle_position": None,
            "engine_load": None,
            "fuel_level": None,
            "battery_voltage": None,
        },
    }
    return payload


def demo_payload() -> Dict[str, object]:
    payload = make_payload()
    payload["gauges"] = {
        "rpm": 3180.0,
        "speed": 62.0,
        "coolant_temp": 88.0,
        "intake_temp": 26.0,
        "map_kpa": 112.0,
        "boost": 1.55,
        "throttle_position": 38.0,
        "engine_load": 46.0,
        "fuel_level": 71.0,
        "battery_voltage": 13.9,
    }
    return payload


def connect_to_adapter(host: str, port: int) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(DEFAULT_TIMEOUT)
    sock.connect((host, port))
    init_elm(sock)
    return sock


def main() -> None:
    parser = argparse.ArgumentParser(description="3DS AutoUI OBD2 bridge")
    parser.add_argument("--host", default=DEFAULT_HOST, help="OBD2 adapter host/IP")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="OBD2 adapter port")
    parser.add_argument("--demo", action="store_true", help="Use sample data instead of connecting to a real adapter")
    args = parser.parse_args()

    if args.demo:
        print(json.dumps(demo_payload(), indent=2))
        return

    try:
        sock = connect_to_adapter(args.host, args.port)
        payload = make_payload()
        payload["gauges"] = fetch_gauges(sock)
        print(json.dumps(payload, indent=2))
    except Exception as exc:  # pragma: no cover - CLI error path
        print(json.dumps({"error": str(exc)}, indent=2))
        raise SystemExit(1)


if __name__ == "__main__":
    main()
