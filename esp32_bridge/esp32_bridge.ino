// ESP32 Wi-Fi bridge between an ELM327 iCar Pro / V-LINK OBD2 adapter and the 3DS AutoUI app.
//
// The ESP32 hosts a single Wi-Fi network (AP). Both the OBD adapter and the 3DS join it
// as clients:
//   * V-LINK/iCar Pro: configured (via its own app/settings) to join this AP as a Wi-Fi
//     client instead of hosting its own network. It keeps running its own ELM327 TCP
//     server, just at whatever IP the ESP32's DHCP hands it out.
//   * 3DS: joins the same AP and connects to the ESP32 on port 35000.
//
// Because the adapter's IP is dynamic (DHCP), the ESP32 auto-discovers it at boot by
// scanning the AP's small IP range and probing each address with an ELM327 handshake
// (see discoverObdAdapter()). No STA/channel-matching is needed at all, which avoids the
// classic ESP32 APSTA channel-mismatch bug where a softAP and a joined network end up on
// different Wi-Fi channels and traffic silently stalls.
//
// A TCP proxy on port 35000 forwards raw ELM327 bytes in both directions and mirrors
// every byte to the USB serial monitor so you can watch the live conversation.
//
// A DNS server (port 53) resolves every hostname to the ESP32's AP address, and a
// tiny HTTP server (port 80) answers the 3DS's connection test at
// conntest.nintendowifi.net so the 3DS believes the network has working internet.
// Without this, the 3DS refuses to treat the AP as usable and won't stay connected.
//
// Board: any ESP32 dev board (WROOM-32, S2, S3, C3 all work). Arduino-ESP32 core.
//
// ---------------------------------------------------------------------------
// TODO (future vehicles):
//   [ ] 2002 Lexus IS300 profile:
//       - Add config/lexus_is300_profile.json (NA 2JZ-GE, no boost gauge, redline ~6600).
//       - Replace boost gauge slot with MAP kPa or vacuum inHg.
//       - Confirm OBD2 support: 2002 IS300 is OBD2/CAN-lite; PID 05/0C/0D/0F/11/04/2F/42 all standard.
//       - PID 5C (oil temp) may return NO DATA on the 2JZ-GE; the gauge will just stay invalid.
// ---------------------------------------------------------------------------

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>

// ---------- User configuration -------------------------------------------------

// The AP both the OBD adapter and the 3DS join. Configure V-LINK's client mode with
// this SSID/password. Keep the password >= 8 chars or WPA2 will refuse it.
static const char* AP_SSID = "AutoUI-ESP32";
static const char* AP_PASS = "autoui3ds";

// TCP port the 3DS connects to on the ESP32, and the port the OBD adapter listens on.
static const uint16_t OBD_PORT = 35000;
static const uint16_t LISTEN_PORT = 35000;

// If you know the adapter's IP will always be the same (e.g. you gave it a static IP
// in its client-mode settings), set it here to skip auto-discovery entirely. Leave ""
// to auto-discover by scanning the AP subnet at boot.
static const char* OBD_HOST_OVERRIDE = "";

// Range of the last IP octet to probe during auto-discovery (matches the ESP32's
// default AP subnet 192.168.4.0/24 with a handful of clients).
static const int DISCOVERY_SCAN_START = 2;
static const int DISCOVERY_SCAN_END = 20;

// Set false if the serial mirror gets too chatty during real driving.
static const bool VERBOSE_LOG = true;

// -------------------------------------------------------------------------------

static WiFiServer proxyServer(LISTEN_PORT);
static String discoveredObdIp;  // cached result of discoverObdAdapter(), empty until found

// 3DS Nintendo Wi-Fi connection test spoofing: DNS resolves every hostname to the
// AP's own IP, and the HTTP server answers conntest.nintendowifi.net with the exact
// response the 3DS expects so it treats this network as internet-connected.
static const byte DNS_PORT = 53;
static DNSServer dnsServer;
static WebServer captivePortal(80);

static void handleCaptivePortalRequest() {
  Serial.println("=== HTTP REQUEST ===");
  Serial.printf("URI: %s\n", captivePortal.uri().c_str());
  Serial.printf("Host header: %s\n", captivePortal.hostHeader().c_str());
  Serial.println("=====================");

  if (captivePortal.hostHeader() == "conntest.nintendowifi.net") {
    const char* html =
        "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" "
        "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">"
        "<html><head><title>HTML Page</title></head>"
        "<body bgcolor=\"#FFFFFF\">This is test.html page</body></html>";
    captivePortal.sendHeader("X-Organization", "Nintendo");
    captivePortal.send(200, "text/html", html);
    return;
  }

  captivePortal.send(200, "text/plain", "OK");
}

static void logDirection(const char* tag, const uint8_t* data, size_t len) {
  if (!VERBOSE_LOG || len == 0) return;
  Serial.printf("%s (%u): ", tag, (unsigned)len);
  for (size_t i = 0; i < len; ++i) {
    uint8_t b = data[i];
    // Show printable ASCII inline (ELM327 is text-based) and escape control bytes.
    if (b == '\r')      Serial.print("\\r");
    else if (b == '\n') Serial.print("\\n");
    else if (b >= 0x20 && b < 0x7f) Serial.write(b);
    else Serial.printf("\\x%02X", b);
  }
  Serial.println();
}

// Tries an ELM327 handshake against a single candidate IP. Returns true and logs the
// reply if something answers on OBD_PORT and sends back any bytes.
static bool probeCandidate(const IPAddress& ip) {
  WiFiClient probe;
  if (!probe.connect(ip, OBD_PORT, 400)) return false;
  probe.print("ATZ\r");
  uint32_t deadline = millis() + 600;
  String reply;
  while (millis() < deadline) {
    while (probe.available()) reply += (char)probe.read();
    if (reply.length() > 0) break;
    delay(10);
  }
  probe.stop();
  if (reply.length() > 0) {
    Serial.printf("[discover]   %s answered: %s\n", ip.toString().c_str(), reply.c_str());
    return true;
  }
  return false;
}

// Scans the AP's small IP range looking for the OBD adapter, since its DHCP-assigned
// IP isn't known ahead of time. Caches the result in discoveredObdIp on success.
static bool discoverObdAdapter() {
  if (OBD_HOST_OVERRIDE[0] != '\0') {
    discoveredObdIp = OBD_HOST_OVERRIDE;
    Serial.printf("[discover] Using configured OBD_HOST_OVERRIDE: %s\n", discoveredObdIp.c_str());
    return true;
  }

  IPAddress apIp = WiFi.softAPIP();
  Serial.printf("[discover] Scanning %s.%d-%d:%u for the OBD adapter (%d clients joined)...\n",
                String(apIp[0]).c_str(), DISCOVERY_SCAN_START, DISCOVERY_SCAN_END, OBD_PORT,
                WiFi.softAPgetStationNum());
  for (int i = DISCOVERY_SCAN_START; i <= DISCOVERY_SCAN_END; ++i) {
    IPAddress candidate(apIp[0], apIp[1], apIp[2], i);
    if (candidate == apIp) continue;
    if (probeCandidate(candidate)) {
      discoveredObdIp = candidate.toString();
      Serial.printf("[discover] Found OBD adapter at %s\n", discoveredObdIp.c_str());
      return true;
    }
  }
  Serial.println("[discover] No OBD adapter found this pass. Confirm V-LINK joined "
                 "'AutoUI-ESP32' and the ignition/adapter is powered.");
  return false;
}

static void startAccessPoint() {
  Serial.printf("[AP]  Starting AP '%s'...\n", AP_SSID);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);
  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("[AP]  3DS should connect to '%s' and target %s:%u\n",
                AP_SSID, apIP.toString().c_str(), LISTEN_PORT);

  // Wildcard DNS: every lookup (including conntest.nintendowifi.net) resolves to us.
  dnsServer.start(DNS_PORT, "*", apIP);

  captivePortal.onNotFound(handleCaptivePortalRequest);
  captivePortal.on("/", handleCaptivePortalRequest);
  captivePortal.begin();
  Serial.println("[AP]  DNS + captive portal HTTP server running.");
}

static bool relayHalfDuplexCounted(WiFiClient& src, WiFiClient& dst, const char* tag, size_t& counter) {
  uint8_t buf[512];
  int avail = src.available();
  if (avail <= 0) return true;
  int toRead = avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail;
  int n = src.read(buf, toRead);
  if (n <= 0) return true;
  logDirection(tag, buf, (size_t)n);
  int written = dst.write(buf, (size_t)n);
  if (written != n) {
    Serial.printf("[proxy] short write on %s: %d/%d\n", tag, written, n);
    return false;
  }
  counter += (size_t)n;
  return true;
}

static void runProxySession(WiFiClient& threeds) {
  if (discoveredObdIp.isEmpty() && !discoverObdAdapter()) {
    Serial.println("[proxy] No known OBD adapter IP yet. Dropping 3DS.");
    threeds.stop();
    return;
  }

  Serial.printf("[proxy] 3DS connected from %s. Opening upstream to OBD adapter at %s:%u...\n",
                threeds.remoteIP().toString().c_str(), discoveredObdIp.c_str(), OBD_PORT);
  WiFiClient obd;
  if (!obd.connect(discoveredObdIp.c_str(), OBD_PORT)) {
    Serial.printf("[proxy] Upstream connect to %s:%u failed. Adapter may have left the "
                  "network or changed IP -- re-discovering.\n", discoveredObdIp.c_str(), OBD_PORT);
    discoveredObdIp = "";
    discoverObdAdapter();
    threeds.stop();
    return;
  }
  Serial.println("[proxy] Upstream established. Relaying...");

  size_t bytesToObd = 0, bytesToThreeds = 0;
  uint32_t lastActivity = millis();

  // Tight loop; ELM327 responses come in bursts terminated by '>'.
  while (threeds.connected() && obd.connected()) {
    size_t beforeObd = bytesToObd, beforeThreeds = bytesToThreeds;
    bool ok = true;
    ok &= relayHalfDuplexCounted(threeds, obd, "[3DS->OBD]", bytesToObd);
    ok &= relayHalfDuplexCounted(obd,    threeds, "[OBD->3DS]", bytesToThreeds);
    if (!ok) break;
    if (bytesToObd != beforeObd || bytesToThreeds != beforeThreeds) {
      lastActivity = millis();
    } else if (millis() - lastActivity > 5000) {
      Serial.printf("[proxy] No traffic for 5s (sent %u to OBD, %u to 3DS so far). "
                    "3DS may be idle or the adapter may have stopped responding.\n",
                    (unsigned)bytesToObd, (unsigned)bytesToThreeds);
      lastActivity = millis();
    }
    // Keep answering DNS/captive-portal checks even while a proxy session is open.
    dnsServer.processNextRequest();
    captivePortal.handleClient();
    delay(1);
  }

  Serial.printf("[proxy] Session closed. Total bytes 3DS->OBD: %u, OBD->3DS: %u\n",
                (unsigned)bytesToObd, (unsigned)bytesToThreeds);
  obd.stop();
  threeds.stop();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("==== 3DS AutoUI ESP32 bridge ====");

  WiFi.mode(WIFI_AP);
  startAccessPoint();

  // Give V-LINK a few seconds to auto-associate and grab a DHCP lease before the
  // first discovery pass.
  delay(3000);
  discoverObdAdapter();

  proxyServer.begin();
  proxyServer.setNoDelay(true);
  Serial.printf("[proxy] Listening on %s:%u\n",
                WiFi.softAPIP().toString().c_str(), LISTEN_PORT);
}

void loop() {
  // Keep retrying discovery in the background until the adapter is found.
  if (discoveredObdIp.isEmpty()) {
    static uint32_t lastDiscoveryAttempt = 0;
    if (millis() - lastDiscoveryAttempt > 10000) {
      lastDiscoveryAttempt = millis();
      discoverObdAdapter();
    }
  }

  // Heartbeat so you can confirm the AP and adapter link are alive without a 3DS session open.
  static uint32_t lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 5000) {
    lastHeartbeat = millis();
    Serial.printf("[status] AP clients:%d OBD:%s heap:%u\n",
                  WiFi.softAPgetStationNum(),
                  discoveredObdIp.isEmpty() ? "not found" : discoveredObdIp.c_str(),
                  (unsigned)ESP.getFreeHeap());
  }

  dnsServer.processNextRequest();
  captivePortal.handleClient();

  WiFiClient client = proxyServer.available();
  if (client) {
    client.setNoDelay(true);
    runProxySession(client);
  }
}
