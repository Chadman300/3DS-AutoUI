# 3DS AutoUI

A starter project for a 3DS car dashboard that reads live OBD2 data from a Vgate iCar Pro Wi‑Fi adapter and renders a Subaru WRX-focused gauge dashboard.

## Architecture

- `bridge/obd_bridge.py`: local bridge that connects to the OBD2 adapter and polls supported PIDs
- `config/subaru_wrx_profile.json`: WRX-specific profile with gauge ranges and theme settings
- `dashboard/demo_dashboard.py`: sample dashboard renderer that prints the normalized gauge data and simulates what the 3DS UI can consume

## Why this structure

The Vgate iCar Pro Wi‑Fi unit is designed for ELM327-compatible apps like Torque, not for direct use as a native 3DS peripheral. This project separates the OBD2 data access from the 3DS UI so the dashboard can be customized without mixing protocol logic into the 3DS app code.

## Quick start

### 1) Create a virtual environment (optional)

```bash
python -m venv .venv
source .venv/bin/activate
```

### 2) Run the bridge in demo mode

```bash
python bridge/obd_bridge.py --demo
```

This prints a sample live dashboard payload without needing a real car or adapter.

### 3) Run the demo dashboard renderer

```bash
python dashboard/demo_dashboard.py --demo
```

### 4) Connect to a real adapter

```bash
python bridge/obd_bridge.py --host 192.168.0.10 --port 35000
```

You will need to confirm the Wi‑Fi adapter IP and port for your Vgate device. The default ELM327-compatible Wi‑Fi port is commonly 35000, but some adapters vary.

## WRX profile

The included `config/subaru_wrx_profile.json` sets:

- brand: Subaru
- model: WRX
- year: 2010
- dashboard accent colors
- safe gauge ranges for RPM, coolant temp, boost, voltage, and more

## Notes

- Boost is derived from MAP kPa when the vehicle does not provide a direct boost PID.
- Oil pressure is not guaranteed on most OBD2 setups; this project treats it as optional and vehicle-specific.
- This is a starter implementation intended to be extended into a real 3DS homebrew UI later.
