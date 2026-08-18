# ESP32 OBD Bridge

Replaces the iPhone / laptop middle-device in the 3DS AutoUI setup. The ESP32
hosts a single Wi-Fi network. Both the V-LINK/iCar Pro OBD adapter and the 3DS
join it as clients. Traffic on TCP port 35000 is proxied byte-for-byte, and a
copy is mirrored to the USB serial monitor for live debugging. A built-in DNS +
HTTP captive portal spoofs the 3DS's internet connection test, since the 3DS
refuses to stay associated with a Wi-Fi network that fails that check.

```
+-----------+       +------------------+       +----------+
| V-LINK/   |  Wi-Fi|  ESP32 (AP only) |  Wi-Fi|  3DS     |
| iCar Pro  |------>|  AutoUI-ESP32    |<----->| 192.168.4.1:35000
| (client)  |       |  192.168.4.1     |       |          |
+-----------+       +------------------+       +----------+
                          |
                          +--> USB Serial (115200 baud) live log
```

The OBD adapter's IP is dynamic (DHCP), so the ESP32 auto-discovers it at boot
by scanning the AP's subnet and probing each address with an ELM327 handshake.
This single-AP design avoids the classic ESP32 dual-role (APSTA) bug where a
hosted AP and a joined network end up on mismatched Wi-Fi channels, silently
stalling traffic.

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

## Configuring V-LINK to join the ESP32

Use V-LINK's own app/settings to switch it from hosting its own network to
joining an existing one as a client:

1. SSID: `AutoUI-ESP32`
2. Password: `autoui3ds`

If V-LINK's settings let you assign it a static/fixed IP in client mode, do
that and set `OBD_HOST_OVERRIDE` in the sketch (see below) — it skips
auto-discovery entirely and is the most reliable option. Otherwise leave it on
DHCP; the ESP32 will find it automatically.

## Configuration

Edit the top of [`esp32_bridge.ino`](esp32_bridge.ino):

| Constant            | Meaning                                              | Default          |
|---------------------|-------------------------------------------------------|------------------|
| AP_SSID             | Network both V-LINK and the 3DS join                 | `AutoUI-ESP32`   |
| AP_PASS             | Password for that network (>= 8 chars)               | `autoui3ds`      |
| OBD_PORT            | ELM327 TCP port on the adapter                       | `35000`          |
| LISTEN_PORT         | Port the 3DS connects to on the ESP32                | `35000`          |
| OBD_HOST_OVERRIDE   | Skip auto-discovery; use this fixed IP instead       | `""` (auto)      |
| DISCOVERY_SCAN_START/END | Last-octet range probed during discovery        | `2`-`20`         |
| VERBOSE_LOG         | Mirror bytes to serial monitor                       | `true`           |

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
4. Power the OBD adapter (ignition on) so it can join the ESP32's network.
5. You should see the ESP32 log:
   ```
   [AP]  Starting AP 'AutoUI-ESP32'...
   [AP]  3DS should connect to 'AutoUI-ESP32' and target 192.168.4.1:35000
   [AP]  DNS + captive portal HTTP server running.
   [discover] Scanning 192.168.4.2-20:35000 for the OBD adapter (1 clients joined)...
   [discover]   192.168.4.2 answered: ELM327 v1.5
   [discover] Found OBD adapter at 192.168.4.2
   [proxy] Listening on 192.168.4.1:35000
   ```
   If discovery doesn't find it on the first pass (adapter still booting), the
   sketch retries automatically every 10s — watch for `[discover]` lines in
   the background `[status]` heartbeat.

## On the 3DS

1. In the 3DS Wi-Fi settings, join `AutoUI-ESP32` (password `autoui3ds`).
2. Launch AutoUI. The default host is `192.168.4.1:35000` — no edit needed
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

- **`[discover] No OBD adapter found this pass.`** — V-LINK hasn't joined
  `AutoUI-ESP32` yet, is unpowered, or its client-mode SSID/password is wrong.
  Check the `AP clients:` count in the `[status]` heartbeat — if it's `0`,
  V-LINK never associated.
- **`[proxy] Upstream connect ... failed.`** — The adapter answered discovery
  earlier but has since dropped off (power cycle, moved out of range). The
  sketch clears the cached IP and re-scans automatically on the next attempt.
- **Discovery keeps failing but `AP clients:` shows 1+** — The adapter may not
  be listening on port 35000 in client mode, or it needs a moment after
  associating before its ELM327 server comes up. Widen
  `DISCOVERY_SCAN_START`/`END` if your AP subnet has more than ~20 possible
  clients, or set `OBD_HOST_OVERRIDE` if you can pin a static IP on V-LINK.
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
