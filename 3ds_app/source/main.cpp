#include <3ds.h>

#include <algorithm>
#include <arpa/inet.h>
#include <malloc.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "dashboard.hpp"
#include "gui_renderer.hpp"
#include "obd_client.hpp"
#include "settings.hpp"

static constexpr size_t SOC_ALIGN = 0x1000;
static constexpr size_t SOC_BUFFER_SIZE = 0x100000;
static u32* g_socBuffer = nullptr;
static bool g_socReady = false;
static bool g_acReady = false;

static void shutdownNetwork() {
    if (g_socReady) {
        socExit();
        g_socReady = false;
    }
    if (g_socBuffer != nullptr) {
        free(g_socBuffer);
        g_socBuffer = nullptr;
    }
    if (g_acReady) {
        acExit();
        g_acReady = false;
    }
}

static void renderFallback(PrintConsole* console, const DashboardData& dashboard,
                           const std::vector<GaugeSample>& samples, const char* error) {
    consoleSelect(console);
    consoleClear();
    printf("3DS AutoUI diagnostic mode\n\nGUI unavailable: %s\n\n", error);
    printf("%s\n%s\n\n", dashboard.brand.c_str(), dashboard.vehicleName.c_str());
    for (const auto& gauge : dashboard.gauges) {
        float value = 0.0f;
        for (const auto& sample : samples) {
            if (sample.id == gauge.id) {
                value = sample.value;
                break;
            }
        }
        printf("%-10s %7.1f %s\n", gauge.label.c_str(), value, gauge.unit.c_str());
    }
}

static void drawBottomStatus(PrintConsole* console, const ObdConnectionConfig& config,
                             const ObdClient& obd, bool liveMode, const GuiSettings& settings,
                             size_t selectedGauge) {
    consoleSelect(console);
    consoleClear();
    printf("3DS AutoUI SETTINGS\n\nOBD2: %s\n", liveMode ? "CONNECTED" : "OFFLINE / SAMPLE");
    printf("Adapter: %s:%u\n", config.host.c_str(), config.port);
    if (!liveMode) printf("Reason: %.38s\n", obd.lastError().c_str());
        printf("\nSelected gauge: %u\n", static_cast<unsigned int>(selectedGauge + 1));
        printf("A visibility  X background  Y accent\n");
        printf("L/R select  B reconnect  START exit\n");
    printf("\nSaved to SD  Theme %u  BG #%06X\n", settings.theme, settings.background);
}

static std::string detectLocalIp(bool networkReady, const ObdConnectionConfig& config) {
    if (!networkReady) return "unknown";

    int probeSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probeSock >= 0) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(config.port));
        addr.sin_addr.s_addr = inet_addr(config.host.c_str());
        if (connect(probeSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            sockaddr_in local{};
            socklen_t len = sizeof(local);
            if (getsockname(probeSock, reinterpret_cast<sockaddr*>(&local), &len) == 0 &&
                local.sin_addr.s_addr != INADDR_ANY) {
                const char* ip = inet_ntoa(local.sin_addr);
                std::string result = (ip != nullptr && ip[0] != '\0') ? ip : std::string();
                close(probeSock);
                if (!result.empty()) return result;
                return "unknown";
            }
        }
        close(probeSock);
    }

    in_addr host{};
    host.s_addr = static_cast<u32>(gethostid());
    if (host.s_addr != 0 && host.s_addr != INADDR_ANY) {
        const char* ip = inet_ntoa(host);
        if (ip != nullptr && ip[0] != '\0') return ip;
    }

    return "unknown";
}

// Re-initializes the SOC service in case the socket context got stuck.
static bool resetSocService() {
    if (g_socReady) {
        socExit();
        g_socReady = false;
    }
    g_socReady = socInit(g_socBuffer, SOC_BUFFER_SIZE) == 0;
    return g_socReady;
}

// Right after socInit() the Wi-Fi stack may not have a route table populated yet,
// causing transient socket()/connect() failures. Retry briefly before giving up.
static bool connectWithRetry(ObdClient& obd, std::string& localIp, bool networkReady,
                             const ObdConnectionConfig& config, int maxAttempts, int delayFrames) {
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        if (!networkReady) break;
        localIp = detectLocalIp(networkReady, config);
        if (obd.connectToAdapter()) return true;
        // If the socket layer itself is failing (not just connect), try resetting SOC once.
        if (attempt == maxAttempts / 2) networkReady = resetSocService();
        for (int frame = 0; frame < delayFrames; ++frame) gspWaitForVBlank();
    }
    localIp = detectLocalIp(networkReady, config);
    return false;
}

static bool handleTouch(touchPosition& touch, DashboardData& dashboard, GuiSettings& settings,
                        size_t& selectedGauge, bool& confirmRevert, int& draggingGauge) {
    hidTouchRead(&touch);
    if (confirmRevert) return true;

    if (touch.py < 36 && touch.px >= 190 && touch.px < 250) {
        settings.editMode = !settings.editMode;
        draggingGauge = -1;
        return true;
    }
    if (touch.py < 36 && touch.px >= 250) {
        confirmRevert = true;
        return true;
    }

    if (settings.editMode) {
        for (size_t index = 0; index < dashboard.gauges.size(); ++index) {
            const float x = settings.x[index] * 0.78f;
            const float y = settings.y[index];
            const float width = index < 2 ? 110.0f : 90.0f;
            const float height = index < 2 ? 48.0f : 34.0f;
            if (touch.px >= x && touch.px <= x + width && touch.py >= y && touch.py <= y + height) {
                selectedGauge = index;
                draggingGauge = static_cast<int>(index);
                return true;
            }
        }
        if (draggingGauge >= 0) {
            settings.x[draggingGauge] = std::max(0.0f, std::min(390.0f, touch.px / 0.78f));
            settings.y[draggingGauge] = std::max(42.0f, std::min(205.0f, static_cast<float>(touch.py)));
            return true;
        }
        return false;
    }

    if (touch.py >= 58 && touch.py < 158) {
        const size_t row = static_cast<size_t>(touch.py - 58) / 25;
        const size_t column = touch.px >= 160 ? 1 : 0;
        const size_t index = row * 2 + column;
        if (index < dashboard.gauges.size()) {
            selectedGauge = index;
            const unsigned int cardX = column == 0 ? 10 : 164;
            if (touch.px >= cardX + 86) settings.dial[index] = !settings.dial[index];
            return true;
        }
    }

    if (touch.py >= 180 && touch.py <= 225 && touch.px >= 28 && touch.px <= 248) {
        const unsigned int channel = static_cast<unsigned int>((touch.py - 180) / 17);
        if (channel < 3) {
            const unsigned int component = static_cast<unsigned int>((touch.px - 28) * 255 / 220);
            unsigned int color = settings.gaugeColor[selectedGauge];
            if (channel == 0) color = (color & 0x00FFFF) | (component << 16);
            if (channel == 1) color = (color & 0xFF00FF) | (component << 8);
            if (channel == 2) color = (color & 0xFFFF00) | component;
            settings.gaugeColor[selectedGauge] = color;
            return true;
        }
    }

    if (touch.py < 36 && touch.px > 250) {
        static const unsigned int backgrounds[] = {0x0B1220, 0x101010, 0x18212B, 0x22141A};
        settings.theme = (settings.theme + 1) % 4;
        settings.background = backgrounds[settings.theme];
        return true;
    }
    return false;
}

// Appends the AC service's current status/error codes to the OBD error so the panel
// shows fresh diagnostics on every attempt, not just the very first one.
static void appendAcDiagnostics(ObdClient& obd) {
    u32 acStatus = 0;
    u32 acError = 0;
    u32 acDetail = 0;
    ACU_GetStatus(&acStatus);
    ACU_GetLastErrorCode(&acError);
    ACU_GetLastDetailErrorCode(&acDetail);
    char withStatus[256];
    snprintf(withStatus, sizeof(withStatus), "%s [AC:%lu ERR:%lu DTL:%lu]", obd.lastError().c_str(),
             static_cast<unsigned long>(acStatus), static_cast<unsigned long>(acError),
             static_cast<unsigned long>(acDetail));
    obd.setError(withStatus);
}

int main(int argc, char** argv) {
    gfxInitDefault();
    DashboardData dashboard = makeWrxDashboard();
    GuiSettings settings;
    loadSettings(settings);
    GuiRenderer gui;
    bool guiMode = gui.init();
    PrintConsole* bottomConsole = guiMode ? nullptr : consoleInit(GFX_BOTTOM, nullptr);

    g_socBuffer = static_cast<u32*>(memalign(SOC_ALIGN, SOC_BUFFER_SIZE));
    // acInit() actually activates the connection for socket use; without it, link-layer
    // Wi-Fi association can succeed while real TCP sockets still fail.
    g_acReady = acInit() == 0;
    // acInit() alone never requests a connection - AC:GetStatus stays "not connected" (1)
    // until something explicitly asks AC to connect via ConnectAsync, allowing any saved AP slot.
    u32 acStatus = 0;
    if (g_acReady) {
        acuConfig config;
        memset(&config, 0, sizeof(config));
        if (R_SUCCEEDED(ACU_CreateDefaultConfig(&config)) &&
            R_SUCCEEDED(ACU_SetAllowApType(&config, AC_AP_TYPE_SLOT1 | AC_AP_TYPE_SLOT2 | AC_AP_TYPE_SLOT3))) {
            Handle connectEvent = 0;
            if (R_SUCCEEDED(svcCreateEvent(&connectEvent, RESET_ONESHOT))) {
                if (R_SUCCEEDED(ACU_ConnectAsync(&config, connectEvent))) {
                    svcWaitSynchronization(connectEvent, 5000000000LL);
                }
                svcCloseHandle(connectEvent);
            }
        }
        for (int frame = 0; frame < 60; ++frame) {
            if (ACU_GetStatus(&acStatus) == 0 && acStatus == 3) break;
            gspWaitForVBlank();
        }
    }
    const bool networkReady = g_acReady && g_socBuffer != nullptr && socInit(g_socBuffer, SOC_BUFFER_SIZE) == 0;
    if (networkReady) {
        g_socReady = true;
        atexit(shutdownNetwork);
    } else if (!guiMode) {
        renderFallback(bottomConsole, dashboard, {}, "network initialization failed");
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            gspWaitForVBlank();
        }
        shutdownNetwork();
        gfxExit();
        return 1;
    }

    std::vector<GaugeSample> samples = {
        {"rpm", 3180.0f},
        {"speed", 62.0f},
        {"coolant_temp", 88.0f},
        {"boost", 1.55f},
        {"battery_voltage", 13.9f},
        {"throttle_position", 38.0f},
        {"engine_load", 46.0f},
        {"fuel_level", 71.0f}
    };

    ObdConnectionConfig obdConfig;
    ObdClient obd(obdConfig);
    std::string localIp = "unknown";
    // Give the Wi-Fi stack a moment to settle before giving up on the first attempt.
    bool liveMode = networkReady && connectWithRetry(obd, localIp, networkReady, obdConfig, 5, 20);
    if (!networkReady) {
        obd.setError("Wi-Fi not connected - check 3DS Internet Settings");
    } else if (!liveMode) {
        appendAcDiagnostics(obd);
    }

    size_t selectedGauge = settings.selected % dashboard.gauges.size();
    bool confirmRevert = false;
    int draggingGauge = -1;
    if (!guiMode) renderFallback(bottomConsole, dashboard, samples, "citro2d initialization failed");
    if (!guiMode) drawBottomStatus(bottomConsole, obdConfig, obd, liveMode, settings, selectedGauge);

    u64 lastPoll = 0;
    while (aptMainLoop()) {
        hidScanInput();
        u32 keys = hidKeysDown();
        touchPosition touch;
        if (keys & KEY_START) break;
        if (confirmRevert && (keys & KEY_A)) {
            GuiSettings defaults;
            settings = defaults;
            selectedGauge = 0;
            confirmRevert = false;
            saveSettings(settings);
        } else if (confirmRevert && (keys & KEY_B)) {
            confirmRevert = false;
        }
        if (keys & KEY_LEFT) selectedGauge = (selectedGauge + dashboard.gauges.size() - 1) % dashboard.gauges.size();
        if (keys & KEY_RIGHT) selectedGauge = (selectedGauge + 1) % dashboard.gauges.size();
        if (!confirmRevert && (keys & KEY_A)) settings.visible[selectedGauge] = !settings.visible[selectedGauge];
        if (keys & KEY_X) {
            static const unsigned int backgrounds[] = {0x0B1220, 0x101010, 0x18212B, 0x22141A};
            settings.theme = (settings.theme + 1) % 4;
            settings.background = backgrounds[settings.theme];
        }
        if (keys & KEY_Y) {
            static const unsigned int accents[] = {0xFF5A36, 0x60A5FA, 0x34D399, 0xF472B6};
            settings.accent = accents[(settings.theme + 1) % 4];
        }
        if ((keys & KEY_B) && networkReady) {
            liveMode = connectWithRetry(obd, localIp, networkReady, obdConfig, 3, 10);
            if (!liveMode) appendAcDiagnostics(obd);
        }
        if ((keys & KEY_TOUCH || hidKeysHeld() & KEY_TOUCH) && handleTouch(touch, dashboard, settings, selectedGauge, confirmRevert, draggingGauge)) {
            settings.selected = static_cast<unsigned int>(selectedGauge);
            saveSettings(settings);
        }
        if (!(hidKeysHeld() & KEY_TOUCH)) draggingGauge = -1;
        if (!confirmRevert && (keys & (KEY_LEFT | KEY_RIGHT | KEY_A | KEY_X | KEY_Y))) {
            settings.selected = static_cast<unsigned int>(selectedGauge);
            saveSettings(settings);
        }

        u64 now = osGetTime();
        if (liveMode && now - lastPoll >= 500) {
            if (!obd.poll(samples)) {
                liveMode = false;
                obd.disconnect();
            }
            lastPoll = now;
        }
        if (guiMode) {
            gui.draw(dashboard, samples, settings, liveMode, selectedGauge, confirmRevert, obd.lastError().c_str(), localIp.c_str());
        }
        else renderFallback(bottomConsole, dashboard, samples, "citro2d initialization failed");
        gspWaitForVBlank();
    }

    obd.disconnect();
    settings.selected = static_cast<unsigned int>(selectedGauge);
    saveSettings(settings);
    gui.shutdown();
    gfxExit();
    return 0;
}
