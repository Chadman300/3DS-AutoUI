// ESP32 OBD cache bridge for the 3DS AutoUI app.
//
// Architecture (dual-core):
//
//   Core 0 - obdPollingTask (FreeRTOS task):
//     Connects to the V-LINK adapter, polls all OBD PIDs as fast as the ECU allows,
//     and stores the latest raw response bytes in pidCache[]. Uses weighted scheduling
//     so fast-changing gauges (RPM, speed, boost, throttle) are polled twice as often.
//
//   Core 1 - Arduino loop / handle3dsClient:
//     The 3DS connects to the ESP32 on port 35000 and issues standard ELM327 commands
//     (ATZ, ATE0, 010C, ...). AT commands and PID queries are answered INSTANTLY from
//     the local cache -- no round-trip to the car. The 3DS sees a perfectly normal
//     ELM327 adapter and requires zero code changes.
//
// No changes to the 3DS app are required. The ELM327 response format is identical.
// The only visible difference is that PID queries return immediately instead of waiting
// ~80ms for the ECU, so the 3DS can poll at its full 15ms inter-query rate (~11 Hz on
// RPM/speed/boost/throttle vs. 1-2 Hz with the old pass-through relay).
//
// Also hosts:
//   * DNS (port 53):  wildcard -> 192.168.4.1 (makes all hostnames resolve to us).
//   * HTTP (port 80): answers conntest.nintendowifi.net so the 3DS passes its
//                     internet-connection test and stays associated.
//
// Board: any ESP32 dev board (WROOM-32, S2, S3, C3). Arduino-ESP32 core.
//
// ---------------------------------------------------------------------------
// TODO (future vehicles):
//   [ ] 2002 Lexus IS300 profile:
//       - Add config/lexus_is300_profile.json (NA 2JZ-GE, no boost gauge, redline ~6600).
//       - Replace boost gauge slot with MAP kPa or vacuum inHg.
//       - IS300 uses ISO 9141-2; standard Mode 01 PIDs 05/0C/0D/0F/11/04/2F/42 work.
//       - PID 5C (oil temp) may return NO DATA on the 2JZ-GE; gauge stays blank.
// ---------------------------------------------------------------------------

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <cstring>

// ---------- User configuration -------------------------------------------------

// The AP both V-LINK and the 3DS join. Set V-LINK client mode to this SSID/password.
static const char*    AP_SSID              = "AutoUI-ESP32";
static const char*    AP_PASS              = "autoui3ds";

// ELM327 TCP port (same on V-LINK and on the ESP32's server for the 3DS).
static const uint16_t OBD_PORT             = 35000;
static const uint16_t LISTEN_PORT          = 35000;

// Set this to skip auto-discovery and use a fixed IP for V-LINK (most reliable).
// Leave "" to auto-discover by scanning the AP subnet at boot.
static const char*    OBD_HOST_OVERRIDE    = "";

// Last-octet range probed during discovery (default AP subnet is 192.168.4.x).
static const int      DISCOVERY_SCAN_START = 2;
static const int      DISCOVERY_SCAN_END   = 20;

// Serve NO DATA to the 3DS for any cache entry older than this.
static const uint32_t CACHE_STALE_MS       = 5000;

// Set false to silence byte-level serial logging during real driving.
static const bool     VERBOSE_LOG          = true;

// -------------------------------------------------------------------------------

// PID table. Indices 0-3 = HIGH priority (fast-changing, polled 2/3 of the time).
//           Indices 4-9 = LOW priority (slow-changing, polled 1/3 of the time).
struct ObdPid { const char* hex; const char* name; bool word; };
static constexpr size_t kObdPidCount = 10;
static constexpr size_t kHighCount   =  4;
static constexpr size_t kLowCount    =  6;

static const ObdPid kObdPids[kObdPidCount] = {
    // HIGH priority
    {"0C", "rpm",             true },
    {"0D", "speed",           false},
    {"0B", "boost_map",       false},
    {"11", "throttle",        false},
    // LOW priority
    {"05", "coolant_temp",    false},
    {"0F", "intake_temp",     false},
    {"04", "engine_load",     false},
    {"2F", "fuel_level",      false},
    {"42", "battery_voltage", true },
    {"5C", "oil_temp",        false},
};

struct CacheSlot { uint8_t b[2]; bool valid; uint32_t ts; };
static CacheSlot         pidCache[kObdPidCount];
static SemaphoreHandle_t cacheMutex;
static String            discoveredObdIp;   // guarded by cacheMutex

// A PID that returns NO DATA/ERROR this many times in a row is almost certainly
// unsupported by the vehicle (e.g. no boost/MAP sensor) -- stop asking for it so real
// ECU round trips go to PIDs that actually answer.
static const int kAutoDisableThreshold = 8;
static bool      pidSupported[kObdPidCount];
static int       pidNoDataStreak[kObdPidCount];

static WiFiServer  proxyServer(LISTEN_PORT);
static const byte  DNS_PORT = 53;
static DNSServer   dnsServer;
static WebServer   captivePortal(80);


// ---- Captive portal (Nintendo connection test) --------------------------------

static void handleCaptivePortalRequest() {
    Serial.println("=== HTTP REQUEST ===");
    Serial.printf("URI: %s\n", captivePortal.uri().c_str());
    Serial.printf("Host: %s\n", captivePortal.hostHeader().c_str());
    Serial.println("===================");
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


// ---- OBD adapter discovery ----------------------------------------------------

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
        Serial.printf("[discover]   %s replied\n", ip.toString().c_str());
        return true;
    }
    return false;
}

static bool discoverObdAdapter() {
    if (OBD_HOST_OVERRIDE[0] != '\0') {
        xSemaphoreTake(cacheMutex, portMAX_DELAY);
        discoveredObdIp = OBD_HOST_OVERRIDE;
        xSemaphoreGive(cacheMutex);
        Serial.printf("[discover] Using OBD_HOST_OVERRIDE: %s\n", OBD_HOST_OVERRIDE);
        return true;
    }
    IPAddress apIp = WiFi.softAPIP();
    Serial.printf("[discover] Scanning .%d-.%d on port %u (%d clients joined)...\n",
                  DISCOVERY_SCAN_START, DISCOVERY_SCAN_END,
                  OBD_PORT, WiFi.softAPgetStationNum());
    for (int i = DISCOVERY_SCAN_START; i <= DISCOVERY_SCAN_END; ++i) {
        IPAddress candidate(apIp[0], apIp[1], apIp[2], i);
        if (candidate == apIp) continue;
        if (probeCandidate(candidate)) {
            xSemaphoreTake(cacheMutex, portMAX_DELAY);
            discoveredObdIp = candidate.toString();
            xSemaphoreGive(cacheMutex);
            Serial.printf("[discover] Found OBD adapter at %s\n", candidate.toString().c_str());
            return true;
        }
    }
    Serial.println("[discover] Not found. Check V-LINK joined 'AutoUI-ESP32' and ignition is on.");
    return false;
}


// ---- AP setup -----------------------------------------------------------------

static void startAccessPoint() {
    Serial.printf("[AP] Starting '%s'...\n", AP_SSID);
    WiFi.softAP(AP_SSID, AP_PASS);
    delay(200);
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[AP] %s   3DS target: %s:%u\n",
                  apIP.toString().c_str(), apIP.toString().c_str(), LISTEN_PORT);
    dnsServer.start(DNS_PORT, "*", apIP);
    captivePortal.onNotFound(handleCaptivePortalRequest);
    captivePortal.on("/", handleCaptivePortalRequest);
    captivePortal.begin();
    Serial.println("[AP] DNS + captive portal running.");
}


// ---- Background OBD polling task (pinned to Core 0) ---------------------------

// Read from a WiFiClient until '>' arrives or timeoutMs elapses.
// Uses vTaskDelay so it yields correctly inside a FreeRTOS task.
static bool readUntilPrompt(WiFiClient& c, String& out, uint32_t timeoutMs) {
    out = "";
    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        while (c.available()) {
            char ch = (char)c.read();
            out += ch;
            if (ch == '>') return true;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return false;
}

static bool elmSend(WiFiClient& c, const char* cmd, String& out, uint32_t timeoutMs = 2000) {
    String s = cmd;
    s += '\r';
    if ((size_t)c.print(s) != s.length()) return false;
    return readUntilPrompt(c, out, timeoutMs);
}

// Extract raw value bytes from an ELM327 mode-01 response.
// Strips spaces, finds "41<pid>", reads 2 or 4 hex chars. Returns byte count (1 or 2) or 0.
static int parseObdBytes(const String& resp, const char* pidHex, uint8_t out[2]) {
    String r = resp;
    r.replace(" ", "");
    r.toUpperCase();
    String marker = "41";
    marker += pidHex;
    int idx = r.indexOf(marker);
    if (idx < 0) return 0;
    const char* p = r.c_str() + idx + marker.length();
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (hv(p[0]) < 0 || hv(p[1]) < 0) return 0;
    out[0] = (uint8_t)((hv(p[0]) << 4) | hv(p[1]));
    if (hv(p[2]) >= 0 && hv(p[3]) >= 0) {
        out[1] = (uint8_t)((hv(p[2]) << 4) | hv(p[3]));
        return 2;
    }
    return 1;
}

static void obdPollingTask(void* /*arg*/) {
    WiFiClient obd;
    String     connectedIp;
    bool       initialized  = false;
    size_t     highIdx = 0, lowIdx = 0;
    int        stepCtr      = 0;
    uint32_t   lastDiscovery = 0;

    for (;;) {
        xSemaphoreTake(cacheMutex, portMAX_DELAY);
        String ip = discoveredObdIp;
        xSemaphoreGive(cacheMutex);

        // Self-rediscovery: if no IP, retry every 10s.
        if (ip.isEmpty()) {
            if (millis() - lastDiscovery > 10000) {
                lastDiscovery = millis();
                discoverObdAdapter();
            } else {
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            continue;
        }

        // (Re)connect if the IP changed or the socket died.
        if (!obd.connected() || ip != connectedIp) {
            obd.stop();
            initialized = false;
            connectedIp = ip;
            Serial.printf("[OBD] Connecting to %s:%u...\n", connectedIp.c_str(), OBD_PORT);
            if (!obd.connect(connectedIp.c_str(), OBD_PORT)) {
                Serial.println("[OBD] Connect failed -- clearing IP for rediscovery.");
                xSemaphoreTake(cacheMutex, portMAX_DELAY);
                discoveredObdIp = "";
                for (size_t i = 0; i < kObdPidCount; ++i) pidCache[i].valid = false;
                xSemaphoreGive(cacheMutex);
                connectedIp = "";
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            Serial.println("[OBD] Connected.");
        }

        // ELM327 initialisation (once per TCP connection).
        if (!initialized) {
            const char* initCmds[] = {"ATZ", "ATE0", "ATL0", "ATS0", "ATH0"};
            bool ok = true;
            for (const char* cmd : initCmds) {
                String resp;
                ok = elmSend(obd, cmd, resp, 3000);
                if (VERBOSE_LOG && ok)
                    Serial.printf("[OBD init] %s -> %s\n", cmd, resp.c_str());
                if (!ok) break;
            }
            if (!ok) {
                Serial.println("[OBD] Init failed, reconnecting...");
                obd.stop();
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            initialized = true;
            Serial.println("[OBD] Ready. Updating cache...");
        }

        // Weighted PID selection: 2 high-priority per 1 low-priority. Skips PIDs already
        // confirmed unsupported so the ECU round trip goes to something that answers.
        size_t pidIdx;
        bool   foundPid = false;
        if ((stepCtr % 3) == 0) {
            for (size_t attempt = 0; attempt < kLowCount; ++attempt) {
                size_t candidate = kHighCount + (lowIdx % kLowCount);
                ++lowIdx;
                if (pidSupported[candidate]) { pidIdx = candidate; foundPid = true; break; }
            }
        } else {
            for (size_t attempt = 0; attempt < kHighCount; ++attempt) {
                size_t candidate = highIdx % kHighCount;
                ++highIdx;
                if (pidSupported[candidate]) { pidIdx = candidate; foundPid = true; break; }
            }
        }
        ++stepCtr;
        if (!foundPid) {
            // Every PID in this tier is unsupported; nothing to poll this step.
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        const ObdPid& pid = kObdPids[pidIdx];
        char cmd[8];
        snprintf(cmd, sizeof(cmd), "01%s", pid.hex);

        String resp;
        if (!elmSend(obd, cmd, resp, 2000)) {
            Serial.printf("[OBD] No response for PID %s -- connection lost.\n", pid.hex);
            obd.stop();
            initialized = false;
            xSemaphoreTake(cacheMutex, portMAX_DELAY);
            discoveredObdIp = "";
            for (size_t i = 0; i < kObdPidCount; ++i) pidCache[i].valid = false;
            xSemaphoreGive(cacheMutex);
            connectedIp = "";
            continue;
        }

        if (VERBOSE_LOG) {
            String r = resp;
            r.replace("\r", "\\r");
            r.replace("\n", "\\n");
            Serial.printf("[cache %s] %s\n", pid.hex, r.c_str());
        }

        bool noData = (resp.indexOf("NO DATA") >= 0 || resp.indexOf("NODATA") >= 0 ||
                       resp.indexOf("ERROR")   >= 0);

        xSemaphoreTake(cacheMutex, portMAX_DELAY);
        if (noData) {
            pidCache[pidIdx].valid = false;
            if (++pidNoDataStreak[pidIdx] >= kAutoDisableThreshold && pidSupported[pidIdx]) {
                pidSupported[pidIdx] = false;
                Serial.printf("[OBD] PID %s got NO DATA %d times -- marking unsupported, "
                              "skipping it from now on.\n", pid.hex, kAutoDisableThreshold);
            }
        } else {
            pidNoDataStreak[pidIdx] = 0;
            uint8_t bytes[2] = {0, 0};
            if (parseObdBytes(resp, pid.hex, bytes) > 0) {
                pidCache[pidIdx].b[0]  = bytes[0];
                pidCache[pidIdx].b[1]  = bytes[1];
                pidCache[pidIdx].valid = true;
                pidCache[pidIdx].ts    = millis();
            }
        }
        xSemaphoreGive(cacheMutex);
        // No explicit delay: the ECU round-trip is the natural rate limiter.
    }
}


// ---- 3DS ELM327 emulator (answers instantly from cache) -----------------------

static String buildPidResponse(const char* pidHex) {
    int slot = -1;
    for (int i = 0; i < (int)kObdPidCount; ++i) {
        if (strcmp(kObdPids[i].hex, pidHex) == 0) { slot = i; break; }
    }
    if (slot < 0) return "NO DATA\r\r>";

    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    CacheSlot e = pidCache[slot];
    xSemaphoreGive(cacheMutex);

    if (!e.valid || (millis() - e.ts) > CACHE_STALE_MS) return "NO DATA\r\r>";

    char buf[32];
    if (kObdPids[slot].word)
        snprintf(buf, sizeof(buf), "41 %s %02X %02X\r\r>", pidHex, e.b[0], e.b[1]);
    else
        snprintf(buf, sizeof(buf), "41 %s %02X\r\r>",      pidHex, e.b[0]);
    return String(buf);
}

static void handle3dsClient(WiFiClient& client) {
    Serial.printf("[3DS] Connected from %s\n", client.remoteIP().toString().c_str());

    String   cmdBuf;
    uint32_t lastActivity       = millis();
    size_t   pidQueriesAnswered = 0;

    while (client.connected()) {
        while (client.available()) {
            char c = (char)client.read();
            lastActivity = millis();

            if (c == '\r' || c == '\n') {
                cmdBuf.trim();
                if (cmdBuf.isEmpty()) { cmdBuf = ""; continue; }

                String upper = cmdBuf;
                upper.toUpperCase();
                if (VERBOSE_LOG) Serial.printf("[3DS->ESP] %s\n", upper.c_str());

                String response;
                if (upper == "ATZ") {
                    response = "ELM327 v1.5\r\r>";
                } else if (upper.startsWith("AT")) {
                    response = "OK\r\r>";
                } else if (upper.startsWith("01") && upper.length() >= 4) {
                    String pidHex = upper.substring(2, 4);
                    response = buildPidResponse(pidHex.c_str());
                    ++pidQueriesAnswered;
                } else {
                    response = "?\r\r>";
                }

                if (VERBOSE_LOG) Serial.printf("[ESP->3DS] %s\n", response.c_str());
                client.print(response);
                cmdBuf = "";
            } else {
                cmdBuf += c;
            }
        }

        if (millis() - lastActivity > 10000) {
            Serial.println("[3DS] Idle timeout.");
            break;
        }

        dnsServer.processNextRequest();
        captivePortal.handleClient();
        delay(1);
    }

    Serial.printf("[3DS] Disconnected after %u PID queries.\n", (unsigned)pidQueriesAnswered);
    client.stop();
}


// ---- setup / loop -------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("==== 3DS AutoUI ESP32 bridge (cache mode) ====");

    cacheMutex = xSemaphoreCreateMutex();
    memset(pidCache, 0, sizeof(pidCache));
    for (size_t i = 0; i < kObdPidCount; ++i) { pidSupported[i] = true; pidNoDataStreak[i] = 0; }

    WiFi.mode(WIFI_AP);
    startAccessPoint();

    // Give V-LINK time to join the AP and obtain a DHCP lease before scanning.
    delay(3000);
    discoverObdAdapter();

    // Start background OBD polling on Core 0 (Wi-Fi/lwIP also lives on Core 0).
    xTaskCreatePinnedToCore(obdPollingTask, "obdPoll", 8192, nullptr, 1, nullptr, 0);

    proxyServer.begin();
    proxyServer.setNoDelay(true);
    Serial.printf("[3DS server] Listening on %s:%u\n",
                  WiFi.softAPIP().toString().c_str(), LISTEN_PORT);
}

void loop() {
    static uint32_t lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 5000) {
        lastHeartbeat = millis();
        xSemaphoreTake(cacheMutex, portMAX_DELAY);
        String ip = discoveredObdIp;
        xSemaphoreGive(cacheMutex);
        int unsupportedCount = 0;
        for (size_t i = 0; i < kObdPidCount; ++i) if (!pidSupported[i]) ++unsupportedCount;
        Serial.printf("[status] AP clients:%d  OBD:%s  unsupported PIDs:%d/%d  heap:%u\n",
                      WiFi.softAPgetStationNum(),
                      ip.isEmpty() ? "searching..." : ip.c_str(),
                      unsupportedCount, (int)kObdPidCount,
                      (unsigned)ESP.getFreeHeap());
    }

    dnsServer.processNextRequest();
    captivePortal.handleClient();

    WiFiClient client = proxyServer.available();
    if (client) {
        client.setNoDelay(true);
        handle3dsClient(client);
    }
}
