# 3DS AutoUI Homebrew App

This folder contains the 3DS homebrew target for the WRX gauge dashboard. It builds a `.3dsx`
application with devkitARM, libctru, and citro2d.

## What it includes

- a branded citro2d top-screen gauge dashboard
- a bottom-screen settings and connection panel
- a dashboard rendering model inspired by the Python prototype
- constants for the Subaru WRX theme and gauge colors
- a stable sample-data mode for first-boot hardware testing

## Prerequisites

To build this on hardware, you need the standard 3DS homebrew toolchain:

- devkitARM
- libctru
- citro2d

From the repository root, run:

```bash
make -C 3ds_app release
```

The build output is `3ds_app/3ds_autoui.3dsx`. Copy it to the 3DS SD card under
`/3ds/3ds_autoui/3ds_autoui.3dsx` and launch it from the Homebrew Launcher.

The current hardware build connects directly to an ELM327-compatible Wi-Fi adapter over
TCP and polls the standard OBD2 PIDs. It defaults to `192.168.4.1:35000`, matching the
ESP32 bridge in [`../esp32_bridge/`](../esp32_bridge/). If you are connecting straight to
an iCar Pro or another middle-device, override the host with `Y` on the settings screen
(common values: `192.168.0.10` for the iCar Pro's own AP, `172.20.10.2` for an iPhone
hotspot). If the adapter cannot be reached, the app remains usable with sample data
and reports the connection error on the bottom screen.

The 3DS must already be connected to the scanner's Wi-Fi access point before launching
the app. The GUI has a console fallback if the GPU cannot initialize.

## GUI controls

- `LEFT` / `RIGHT`: select a gauge
- `A`: toggle the selected gauge's visibility
- `X`: cycle background presets
- `Y`: cycle accent colors
- `B`: reconnect to the scanner
- `START`: exit

The bottom screen is touch-enabled:

- Tap a gauge card to select it.
- Tap a gauge card's `DIAL` checkbox to choose dial or bar display for that gauge.
- Tap along the red, green, or blue bar to set that channel directly.
- Tap the top-right header area to cycle background presets.
- Tap `EDIT` to enter layout editing, then touch and drag gauge cards in the preview.
- Tap `RESET` to open the revert confirmation; press `A` to confirm or `B` to cancel.
- The selected gauge's RGB and hex values appear below the sliders.

Settings are saved at `sdmc:/3ds/3ds_autoui.cfg` and restored on the next launch. The
current build provides background presets and custom gauge/accent colors; SD-card image
background loading can be added after the GUI is validated on hardware.

## Hardware test checklist

1. Join the Vgate scanner's Wi-Fi network on the 3DS.
2. Confirm the adapter IP and port are `192.168.4.1:35000` (ESP32 bridge) or update the
   host with `Y` on the settings screen.
3. Confirm the `.3dsx` launches without crashing.
4. Confirm the bottom screen reports `OBD2: connected`.
5. Confirm the top-screen values update approximately twice per second with the engine running.
6. Press `START` and confirm the app exits cleanly.
