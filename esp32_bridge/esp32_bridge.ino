// ESP32 Wi-Fi bridge between an ELM327 iCar Pro OBD2 adapter and the 3DS AutoUI app.
//
// The ESP32 runs in dual APSTA mode:
//   * STA: joins the iCar Pro's own Wi-Fi network (the dongle stays in AP mode).
//   * AP:  hosts a separate Wi-Fi network the 3DS connects to.
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

// iCar Pro Wi-Fi credentials. Most iCar Pro units broadcast an open SSID like
// "WiFi_OBDII" or "V-LINK". Set OBD_PASS to "" for open networks.
static const char* OBD_SSID = "WiFi_OBDII";
static const char* OBD_PASS = "";

// The iCar Pro's TCP endpoint on its own network.
static const char*    OBD_HOST = "192.168.0.10";
static const uint16_t OBD_PORT = 35000;

// The AP the 3DS will join. Keep the password >= 8 chars or WPA2 will refuse it.
static const char* AP_SSID = "AutoUI-ESP32";
static const char* AP_PASS = "autoui3ds";

// TCP port the 3DS connects to on the ESP32's AP interface. Match the app default.
static const uint16_t LISTEN_PORT = 35000;

// Set false if the serial mirror gets too chatty during real driving.
static const bool VERBOSE_LOG = true;

// -------------------------------------------------------------------------------

static WiFiServer proxyServer(LISTEN_PORT);

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

static bool joinObdNetwork() {
  Serial.printf("[STA] Joining OBD network '%s'...\n", OBD_SSID);
  WiFi.begin(OBD_SSID, OBD_PASS);
  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - started > 20000) {
      Serial.println("[STA] Timeout joining OBD network.");
      return false;
    }
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  Serial.printf("[STA] Connected. ESP32 IP on OBD net: %s (gateway %s)\n",
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str());
  return true;
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

static bool relayHalfDuplex(WiFiClient& src, WiFiClient& dst, const char* tag) {
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
  return true;
}

static void runProxySession(WiFiClient& threeds) {
  Serial.println("[proxy] 3DS connected. Opening upstream to iCar Pro...");
  WiFiClient obd;
  if (!obd.connect(OBD_HOST, OBD_PORT)) {
    Serial.printf("[proxy] Upstream connect to %s:%u failed. Dropping 3DS.\n",
                  OBD_HOST, OBD_PORT);
    threeds.stop();
    return;
  }
  Serial.println("[proxy] Upstream established. Relaying...");

  // Tight loop; ELM327 responses come in bursts terminated by '>'.
  while (threeds.connected() && obd.connected()) {
    bool ok = true;
    ok &= relayHalfDuplex(threeds, obd, "[3DS->OBD]");
    ok &= relayHalfDuplex(obd,    threeds, "[OBD->3DS]");
    if (!ok) break;
    // Keep answering DNS/captive-portal checks even while a proxy session is open.
    dnsServer.processNextRequest();
    captivePortal.handleClient();
    delay(1);
  }

  Serial.println("[proxy] Session closed.");
  obd.stop();
  threeds.stop();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("==== 3DS AutoUI ESP32 bridge ====");

  WiFi.mode(WIFI_AP_STA);

  startAccessPoint();

  if (!joinObdNetwork()) {
    // The AP stays up even without upstream, so the 3DS can still connect and
    // see a "connect failed" message instead of nothing. Retry indefinitely.
    Serial.println("[STA] Will keep retrying in the main loop.");
  }

  proxyServer.begin();
  proxyServer.setNoDelay(true);
  Serial.printf("[proxy] Listening on %s:%u\n",
                WiFi.softAPIP().toString().c_str(), LISTEN_PORT);
}

void loop() {
  // Keep STA link alive; iCar Pro APs sometimes drop clients on reset.
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastRetry = 0;
    if (millis() - lastRetry > 5000) {
      lastRetry = millis();
      joinObdNetwork();
    }
  }

  dnsServer.processNextRequest();
  captivePortal.handleClient();

  WiFiClient client = proxyServer.available();
  if (client) {
    client.setNoDelay(true);
    runProxySession(client);
  }
}
