# ESP32 OBD Bridge

Replaces the iPhone / laptop middle-device in the 3DS AutoUI setup. The ESP32
joins the iCar Pro's Wi-Fi as a client and simultaneously hosts its own Wi-Fi
network for the 3DS. Traffic on TCP port 35000 is proxied byte-for-byte, and a
copy is mirrored to the USB serial monitor for live debugging. A built-in DNS +
HTTP captive portal spoofs the 3DS's internet connection test, since the 3DS
refuses to stay associated with a Wi-Fi network that fails that check.

```
+-----------+       +------------------+       +----------+
| iCar Pro  |  Wi-Fi| ESP32 (APSTA)    |  Wi-Fi|  3DS     |
| 192.168.0.10:35000|<--- STA joins    |       |          |
|           |       |  AP: AutoUI-ESP32|<----->| 192.168.4.1:35000
+-----------+       +------------------+       +----------+
                          |
                          +--> USB Serial (115200 baud) live log
```

## Hardware

- Any ESP32 dev board (WROOM-32, S2, S3, C3). USB-C boards are easiest.
- USB cable for flashing + serial monitor.
- 5V USB power source in the car (cig lighter USB adapter is fine).

## Prerequisites

Install the Arduino IDE 2.x and add the ESP32 board package:

1. `File > Preferences > Additional Board Manager URLs`, add:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. `Tools > Board > Boards Manager`, install **esp32 by Espressif Systems**.
3. `Tools > Board`, pick your board (e.g. `ESP32 Dev Module`).

PlatformIO works too — just point it at `esp32_bridge.ino` with framework `arduino`.

## Configuration

Edit the top of [`esp32_bridge.ino`](esp32_bridge.ino):

| Constant   | Meaning                                      | Default          |
|------------|----------------------------------------------|------------------|
| OBD_SSID   | The iCar Pro's own Wi-Fi SSID                | `WiFi_OBDII`     |
| OBD_PASS   | iCar Pro password (empty for open networks)  | `""`             |
| OBD_HOST   | iCar Pro's TCP IP on its own AP              | `192.168.0.10`   |
| OBD_PORT   | ELM327 TCP port                              | `35000`          |
| AP_SSID    | Network the 3DS joins                        | `AutoUI-ESP32`   |
| AP_PASS    | Password for the 3DS network (>= 8 chars)    | `autoui3ds`      |
| LISTEN_PORT| Port the 3DS connects to on the ESP32        | `35000`          |
| VERBOSE_LOG| Mirror bytes to serial monitor               | `true`           |

Confirm your iCar Pro SSID by looking at the sticker on the dongle or scanning
Wi-Fi networks with the ignition on.

## 3DS internet connection test spoofing

The 3DS checks connectivity by requesting `http://conntest.nintendowifi.net/`
and expects an exact response with an `X-Organization: Nintendo` header. If
that check fails, the 3DS will not treat the AP as usable Wi-Fi and won't stay
connected. The sketch runs:

- A **DNS server** on port 53 that resolves every hostname (wildcard `*`) to
  the ESP32's own AP address (`192.168.4.1`).
- An **HTTP server** on port 80 that answers any request to
  `conntest.nintendowifi.net` with the exact HTML/header pair the 3DS expects,
  and returns a generic `200 OK` for anything else.

No configuration needed — this starts automatically with the AP. Every request
is also logged to serial as `=== HTTP REQUEST ===` with the URI and Host header,
so you can confirm the 3DS is hitting the expected hostname.

## Flash & run

1. Plug the ESP32 into your PC, pick the right COM port under `Tools > Port`.
2. Click Upload.
3. Open `Tools > Serial Monitor` at **115200 baud**.
4. Power the iCar Pro (ignition on).
5. You should see the ESP32 log:
   ```
   [AP]  Starting AP 'AutoUI-ESP32'...
   [AP]  3DS should connect to 'AutoUI-ESP32' and target 192.168.4.1:35000
   [STA] Joining OBD network 'WiFi_OBDII'...
   [STA] Connected. ESP32 IP on OBD net: 192.168.0.11 (gateway 192.168.0.10)
   [proxy] Listening on 192.168.4.1:35000
   ```

## On the 3DS

1. In the 3DS Wi-Fi settings, join `AutoUI-ESP32` (password `autoui3ds`).
2. Launch AutoUI. The default host is now `192.168.4.1:35000` — no edit needed
   unless you changed the ESP32 defaults.
3. You should see `OBD2: connected` on the bottom screen and the top screen
   should start updating a couple times per second.

## Watching the traffic

With `VERBOSE_LOG = true`, every request from the 3DS and every response from
the iCar Pro is printed to the serial monitor. ELM327 is a plain-text protocol,
so you'll see things like:

```
[3DS->OBD] (5): ATZ\r
[OBD->3DS] (23): ELM327 v1.5\r\r>
[3DS->OBD] (5): ATE0\r
[OBD->3DS] (7): OK\r\r>
[3DS->OBD] (5): 010C\r
[OBD->3DS] (16): 41 0C 1A F8\r\r>
```

`41 0C 1A F8` = response to PID `0C` (RPM). Raw word `0x1AF8` / 4 = 1726 RPM.

Set `VERBOSE_LOG = false` and re-flash if you want to quiet the log for real
driving.

## Troubleshooting

- **`[STA] Timeout joining OBD network.`** — Wrong SSID/password, or the iCar Pro
  isn't powered. The AP stays up so you can still connect the 3DS and read the
  error message.
- **`[proxy] Upstream connect to 192.168.0.10:35000 failed.`** — The ESP32 joined
  the OBD network but the dongle isn't answering. Check `OBD_HOST` matches the
  gateway address shown in the STA log (some iCar Pro clones use `192.168.4.1`).
- **3DS shows connect timeout** — Make sure the 3DS actually joined `AutoUI-ESP32`
  and not your home Wi-Fi. The 3DS system Wi-Fi settings can remember multiple
  networks and pick the wrong one.

## TODO — future vehicles

- [ ] **2002 Lexus IS300 profile**
  - Add `config/lexus_is300_profile.json` mirroring `subaru_wrx_profile.json`.
  - Replace boost gauge with MAP kPa (or vacuum inHg) — the 2JZ-GE is NA.
  - Redline around 6600 RPM, coolant normal 82-93 C, fuel level supported.
  - Oil temp (PID 5C) may return NO DATA on 2JZ-GE — the app already handles
    that gracefully by leaving the gauge invalid.
  - Confirm the IS300 responds to standard OBD2 Mode 01 PIDs (it should — all
    2001+ US-market cars are OBD2 compliant; the IS300 uses ISO 9141-2 which
    the ELM327 auto-detects).
  - Update the vehicle name / theme colors in the profile JSON.
