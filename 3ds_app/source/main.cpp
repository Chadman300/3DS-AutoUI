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

static void drawBottomStatus(PrintConsole* console, const char* host, unsigned int port,
                             bool liveMode, const char* error, const GuiSettings& settings,
                             size_t selectedGauge) {
    consoleSelect(console);
    consoleClear();
    printf("3DS AutoUI SETTINGS\n\nOBD2: %s\n", liveMode ? "CONNECTED" : "OFFLINE / SAMPLE");
    printf("Adapter: %s:%u\n", host, port);
    if (!liveMode) printf("Reason: %.38s\n", error);
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

static bool handleTouch(touchPosition& touch, DashboardData& dashboard, GuiSettings& settings,
                        size_t& selectedGauge, bool& confirmRevert, int& draggingGauge,
                        bool liveMode, bool& showConnectionError) {
    hidTouchRead(&touch);
    if (confirmRevert) return true;

    if (!liveMode && showConnectionError) {
        if (touch.px < 56 && touch.py < 48) {
            showConnectionError = false;
            return true;
        }
        return false;
    }

    if (touch.py < 42 && touch.px >= 200 && touch.px < 255) {
        if (!liveMode) showConnectionError = true;
        return true;
    }

    if (touch.py < 36 && touch.px >= 150 && touch.px < 200) {
        settings.editMode = !settings.editMode;
        draggingGauge = -1;
        return true;
    }
    if (touch.py < 36 && touch.px >= 255) {
        confirmRevert = true;
        return true;
    }

    if (settings.editMode) {
        constexpr float EDIT_SCALE = 0.58f;
        constexpr float EDIT_OFFSET_X = 10.0f;
        constexpr float EDIT_OFFSET_Y = 42.0f;

        // Toolbar: scale slider minus button (y=192-210, x=8-26)
        if (touch.py >= 192 && touch.py <= 210 && touch.px >= 8 && touch.px < 26) {
            if (settings.bannerSelected) {
                settings.bannerScale = std::max(0.5f, settings.bannerScale - 0.1f);
            } else {
                settings.scale[selectedGauge] = std::max(0.5f, settings.scale[selectedGauge] - 0.1f);
            }
            return true;
        }
        // Toolbar: scale slider plus button (y=192-210, x=128-146)
        if (touch.py >= 192 && touch.py <= 210 && touch.px >= 128 && touch.px < 146) {
            if (settings.bannerSelected) {
                settings.bannerScale = std::min(2.0f, settings.bannerScale + 0.1f);
            } else {
                settings.scale[selectedGauge] = std::min(2.0f, settings.scale[selectedGauge] + 0.1f);
            }
            return true;
        }
        // Toolbar: scale slider track drag (y=196-208, x=30-126)
        if (touch.py >= 196 && touch.py <= 208 && touch.px >= 30 && touch.px <= 126) {
            float newScale = 0.5f + 1.5f * static_cast<float>(touch.px - 30) / 96.0f;
            newScale = std::max(0.5f, std::min(2.0f, newScale));
            if (settings.bannerSelected) settings.bannerScale = newScale;
            else settings.scale[selectedGauge] = newScale;
            return true;
        }
        // Toolbar: visibility toggle (y=192-210, x=174-234)
        if (touch.py >= 192 && touch.py <= 210 && touch.px >= 174 && touch.px <= 234) {
            if (settings.bannerSelected) settings.bannerVisible = !settings.bannerVisible;
            else settings.visible[selectedGauge] = !settings.visible[selectedGauge];
            return true;
        }
        // Toolbar: dial/bar toggle (y=192-210, x=240-300), not for banner
        if (!settings.bannerSelected && touch.py >= 192 && touch.py <= 210 && touch.px >= 240 && touch.px <= 300) {
            settings.dial[selectedGauge] = !settings.dial[selectedGauge];
            return true;
        }

        // If already dragging, keep moving the same element (prevents switching on overlap)
        if (draggingGauge == -2) {
            settings.bannerX = std::max(0.0f, std::min(390.0f,
                (static_cast<float>(touch.px) - EDIT_OFFSET_X) / EDIT_SCALE));
            settings.bannerY = std::max(0.0f, std::min(205.0f,
                (static_cast<float>(touch.py) - EDIT_OFFSET_Y) / EDIT_SCALE));
            return true;
        }
        if (draggingGauge >= 0) {
            settings.x[draggingGauge] = std::max(0.0f, std::min(390.0f,
                (static_cast<float>(touch.px) - EDIT_OFFSET_X) / EDIT_SCALE));
            settings.y[draggingGauge] = std::max(0.0f, std::min(205.0f,
                (static_cast<float>(touch.py) - EDIT_OFFSET_Y) / EDIT_SCALE));
            return true;
        }

        // Banner hit test (center-based)
        {
            const float bw = 384.0f * settings.bannerScale * EDIT_SCALE;
            const float bh = 38.0f * settings.bannerScale * EDIT_SCALE;
            const float bx = settings.bannerX * EDIT_SCALE + EDIT_OFFSET_X - bw * 0.5f;
            const float by = settings.bannerY * EDIT_SCALE + EDIT_OFFSET_Y - bh * 0.5f;
            if (touch.px >= bx && touch.px <= bx + bw && touch.py >= by && touch.py <= by + bh) {
                settings.bannerSelected = true;
                draggingGauge = -2;
                return true;
            }
        }

        // Gauge box hit tests (center-based)
        for (size_t index = 0; index < dashboard.gauges.size(); ++index) {
            const float s = settings.scale[index];
            const float baseW = index < 2 ? 188.0f : 92.0f;
            const float baseH = index < 2 ? 78.0f : 43.0f;
            const float w = baseW * s * EDIT_SCALE;
            const float h = baseH * s * EDIT_SCALE;
            const float x = settings.x[index] * EDIT_SCALE + EDIT_OFFSET_X - w * 0.5f;
            const float y = settings.y[index] * EDIT_SCALE + EDIT_OFFSET_Y - h * 0.5f;
            if (touch.px >= x && touch.px <= x + w && touch.py >= y && touch.py <= y + h) {
                selectedGauge = index;
                settings.bannerSelected = false;
                draggingGauge = static_cast<int>(index);
                return true;
            }
        }

        return false;
    }

    // Non-edit mode: gauge selection + color picker (no dial toggle here)
    if (touch.py >= 58 && touch.py < 158) {
        const size_t row = static_cast<size_t>(touch.py - 58) / 25;
        const size_t column = touch.px >= 160 ? 1 : 0;
        const size_t index = row * 2 + column;
        if (index < dashboard.gauges.size()) {
            selectedGauge = index;
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

// Writes the AC service status/error codes after a base message, for on-screen diagnostics.
static void buildAcDiag(const char* base, char* out, size_t outSize) {
    u32 acStatus = 0, acError = 0, acDetail = 0;
    ACU_GetStatus(&acStatus);
    ACU_GetLastErrorCode(&acError);
    ACU_GetLastDetailErrorCode(&acDetail);
    snprintf(out, outSize, "%s [AC:%lu ERR:%lu DTL:%lu]", base,
             static_cast<unsigned long>(acStatus), static_cast<unsigned long>(acError),
             static_cast<unsigned long>(acDetail));
}

// Shared state between the render (main) thread and the background network thread. All
// socket I/O runs on the network thread so the UI never blocks on a slow adapter/Wi-Fi.
struct NetThread {
    LightLock lock;
    ObdClient* obd = nullptr;
    ObdConnectionConfig config;
    bool networkReady = false;
    std::vector<GaugeSample> samples;
    char localIp[24] = "unknown";
    char error[256] = "";
    char vehicleBrand[24] = "";
    char vehicleName[48] = "";
    volatile bool vinReady = false;
    volatile bool running = true;
    volatile bool live = false;
    volatile bool initialConnectionComplete = false;
    volatile bool reconnectRequested = false;
};

// Decodes the model year from VIN position 10 (0 if unknown).
static int vinModelYear(char c) {
    if (c >= '1' && c <= '9') return 2000 + (c - '0');
    switch (c) {
        case 'A': return 2010; case 'B': return 2011; case 'C': return 2012;
        case 'D': return 2013; case 'E': return 2014; case 'F': return 2015;
        case 'G': return 2016; case 'H': return 2017; case 'J': return 2018;
        case 'K': return 2019; case 'L': return 2020; case 'M': return 2021;
        case 'N': return 2022; case 'P': return 2023; case 'R': return 2024;
        case 'S': return 2025; case 'T': return 2026; case 'V': return 2027;
        case 'W': return 2028; case 'X': return 2029; case 'Y': return 2030;
    }
    return 0;
}

// Maps the VIN's World Manufacturer Identifier (first 3 chars) to a make name.
static std::string vinMake(const std::string& vin) {
    if (vin.size() < 3) return "VEHICLE";
    const std::string w = vin.substr(0, 3);
    const std::string w2 = vin.substr(0, 2);
    if (w == "JF1" || w == "JF2" || w == "4S3" || w == "4S4") return "SUBARU";
    if (w2 == "1H" || w2 == "JH" || w == "2HG" || w == "3HG") return "HONDA";
    if (w == "JTD" || w2 == "JT" || w2 == "4T" || w2 == "5T" || w2 == "2T") return "TOYOTA";
    if (w2 == "1F" || w2 == "2F" || w2 == "3F") return "FORD";
    if (w2 == "1G" || w == "KL1" || w2 == "2G" || w2 == "3G") return "GM";
    if (w2 == "1N" || w2 == "JN" || w == "3N1" || w == "5N1") return "NISSAN";
    if (w == "WBA" || w == "WBS" || w == "5UX" || w == "4US") return "BMW";
    if (w == "WDB" || w == "WDD" || w == "WDC" || w == "4JG") return "MERCEDES";
    if (w == "WVW" || w == "3VW" || w == "1VW" || w == "WVG") return "VOLKSWAGEN";
    if (w == "WAU" || w == "TRU" || w == "WA1") return "AUDI";
    if (w == "KMH" || w == "KMF" || w == "5NP") return "HYUNDAI";
    if (w == "KND" || w == "5XY" || w == "KNA") return "KIA";
    if (w == "JM1" || w == "JM3" || w2 == "4F") return "MAZDA";
    return w; // unknown make: show the raw WMI
}

// Runs on the network thread only. Connects (with a couple of quick retries) and publishes
// the result to shared state. Uses svcSleepThread for delays (never graphics calls).
static void netConnect(NetThread* nt) {
    if (!nt->networkReady) return;
    ObdConnectionConfig cfg;
    LightLock_Lock(&nt->lock);
    cfg = nt->config;
    LightLock_Unlock(&nt->lock);
    nt->obd->setConfig(cfg);

    bool live = false;
    for (int attempt = 0; attempt < 2 && !live; ++attempt) {
        live = nt->obd->connectToAdapter();
        if (!live) svcSleepThread(300 * 1000000LL);
    }
    std::string ip = detectLocalIp(true, cfg);

    LightLock_Lock(&nt->lock);
    nt->live = live;
    snprintf(nt->localIp, sizeof(nt->localIp), "%s", ip.c_str());
    if (live) {
        for (auto& sample : nt->samples) sample.valid = false;
        nt->error[0] = '\0';
    } else {
        buildAcDiag(nt->obd->lastError().c_str(), nt->error, sizeof(nt->error));
    }
    LightLock_Unlock(&nt->lock);

    // Read and decode the VIN once per successful connect.
    std::string brand;
    std::string name;
    bool gotVin = false;
    if (live) {
        std::string vin;
        if (nt->obd->queryVin(vin)) {
            brand = vinMake(vin);
            const int year = vinModelYear(vin.size() >= 10 ? vin[9] : '0');
            char buf[48];
            if (year > 0) snprintf(buf, sizeof(buf), "%d  %s", year, vin.c_str());
            else snprintf(buf, sizeof(buf), "%s", vin.c_str());
            name = buf;
            gotVin = true;
        }
    }

    if (gotVin) {
        LightLock_Lock(&nt->lock);
        snprintf(nt->vehicleBrand, sizeof(nt->vehicleBrand), "%s", brand.c_str());
        snprintf(nt->vehicleName, sizeof(nt->vehicleName), "%s", name.c_str());
        nt->vinReady = true;
        LightLock_Unlock(&nt->lock);
    }
}

static void netThreadMain(void* arg) {
    NetThread* nt = static_cast<NetThread*>(arg);
    netConnect(nt);
    nt->initialConnectionComplete = true;
    while (nt->running) {
        if (nt->reconnectRequested) {
            nt->reconnectRequested = false;
            netConnect(nt);
            continue;
        }
        if (nt->live) {
            std::vector<GaugeSample> local;
            LightLock_Lock(&nt->lock);
            local = nt->samples;
            LightLock_Unlock(&nt->lock);

            bool lost = false;
            const bool ok = nt->obd->pollStep(local, lost);

            LightLock_Lock(&nt->lock);
            nt->samples = local;
            if (!ok && lost) {
                nt->live = false;
                buildAcDiag(nt->obd->lastError().c_str(), nt->error, sizeof(nt->error));
            }
            LightLock_Unlock(&nt->lock);
            if (!ok && lost) nt->obd->disconnect();
            // The ESP32 cache answers near-instantly, so this only needs to yield the
            // thread, not throttle for a slow physical adapter anymore.
            svcSleepThread(3 * 1000000LL);
        } else {
            svcSleepThread(120 * 1000000LL); // idle while disconnected
        }
    }
    nt->obd->disconnect();
}

int main(int argc, char** argv) {
    gfxInitDefault();
    romfsInit();
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
        {"fuel_level", 71.0f},
        {"oil_temp", 96.0f},
        {"oil_pressure", 45.0f}
    };

    ObdConnectionConfig obdConfig;
    obdConfig.host = settings.host;
    obdConfig.port = static_cast<u16>(settings.port);
    ObdClient obd(obdConfig);

    // Hidden gauges don't need live data at all -- skip polling them so their PID slot
    // goes to gauges the user actually has on screen.
    for (size_t i = 0; i < dashboard.gauges.size() && i < 10; ++i) {
        obd.setPidEnabled(dashboard.gauges[i].id.c_str(), settings.visible[i]);
    }

    // All socket I/O runs on a background thread so the render loop never blocks on the adapter.
    NetThread net;
    LightLock_Init(&net.lock);
    net.obd = &obd;
    net.config = obdConfig;
    net.networkReady = networkReady;
    net.samples = samples;
    if (!networkReady) {
        snprintf(net.error, sizeof(net.error), "Wi-Fi not connected - check 3DS Internet Settings");
    }
    s32 mainPrio = 0x30;
    svcGetThreadPriority(&mainPrio, CUR_THREAD_HANDLE);
    Thread netThread = threadCreate(netThreadMain, &net, 32 * 1024, mainPrio + 1, -2, false);

    size_t selectedGauge = settings.selected % dashboard.gauges.size();
    bool confirmRevert = false;
    int draggingGauge = -1;
    float errorScroll = 0.0f;
    bool showConnectionError = true;
    bool previousLiveMode = false;
    unsigned int loadingFrame = 0;

    while (aptMainLoop()) {
        hidScanInput();
        u32 keys = hidKeysDown();
        touchPosition touch;
        if (keys & KEY_START) break;

        // Snapshot the background thread's state once per frame (cheap copy under the lock).
        bool liveMode;
        std::vector<GaugeSample> frameSamples;
        char frameIp[24];
        char frameError[256];
        LightLock_Lock(&net.lock);
        liveMode = net.live;
        frameSamples = net.samples;
        snprintf(frameIp, sizeof(frameIp), "%s", net.localIp);
        snprintf(frameError, sizeof(frameError), "%s", net.error);
        // Apply the scanned VIN (make + year + VIN) to the header once it's available.
        if (net.vinReady) {
            dashboard.brand = net.vehicleBrand;
            dashboard.vehicleName = net.vehicleName;
            net.vinReady = false;
        }
        LightLock_Unlock(&net.lock);

        const bool loading = !net.initialConnectionComplete;
        if (loading) {
            gui.drawLoading(loadingFrame++);
            gspWaitForVBlank();
            continue;
        }

        if (!liveMode && previousLiveMode) showConnectionError = true;
        previousLiveMode = liveMode;

        // Capture revert state at frame start so B can't both cancel a revert and reconnect.
        const bool revertActive = confirmRevert;
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
        if (!liveMode && (keys & (KEY_UP | KEY_CPAD_UP))) errorScroll = std::max(0.0f, errorScroll - 32.0f);
        if (!liveMode && (keys & (KEY_DOWN | KEY_CPAD_DOWN))) errorScroll += 32.0f;
        if (!confirmRevert && (keys & KEY_A)) {
            settings.visible[selectedGauge] = !settings.visible[selectedGauge];
            if (selectedGauge < dashboard.gauges.size()) {
                obd.setPidEnabled(dashboard.gauges[selectedGauge].id.c_str(), settings.visible[selectedGauge]);
            }
        }
        if (keys & KEY_X) {
            static const unsigned int backgrounds[] = {0x0B1220, 0x101010, 0x18212B, 0x22141A};
            settings.theme = (settings.theme + 1) % 4;
            settings.background = backgrounds[settings.theme];
        }
        if (keys & KEY_Y) {
            if (!liveMode && networkReady) {
                char buffer[16];
                snprintf(buffer, sizeof(buffer), "%s", settings.host);
                SwkbdState swkbd;
                swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 15);
                swkbdSetHintText(&swkbd, "Adapter IP e.g. 192.168.4.1");
                SwkbdButton button = swkbdInputText(&swkbd, buffer, sizeof(buffer));
                if (button != SWKBD_BUTTON_NONE && buffer[0] != '\0') {
                    snprintf(settings.host, sizeof(settings.host), "%s", buffer);
                    saveSettings(settings);
                    errorScroll = 0.0f;
                    // Hand the new host to the network thread and ask it to reconnect.
                    LightLock_Lock(&net.lock);
                    net.config.host = settings.host;
                    LightLock_Unlock(&net.lock);
                    net.reconnectRequested = true;
                }
            } else {
                static const unsigned int accents[] = {0xFF5A36, 0x60A5FA, 0x34D399, 0xF472B6};
                settings.accent = accents[(settings.theme + 1) % 4];
            }
        }
        if ((keys & KEY_B) && networkReady && !liveMode && !revertActive) {
            errorScroll = 0.0f;
            LightLock_Lock(&net.lock);
            net.config.host = settings.host;
            LightLock_Unlock(&net.lock);
            net.reconnectRequested = true;
        }
        if ((keys & KEY_TOUCH || hidKeysHeld() & KEY_TOUCH) &&
            handleTouch(touch, dashboard, settings, selectedGauge, confirmRevert, draggingGauge,
                        liveMode, showConnectionError)) {
            settings.selected = static_cast<unsigned int>(selectedGauge);
            saveSettings(settings);
        }
        if (!(hidKeysHeld() & KEY_TOUCH)) draggingGauge = -1;
        if (!confirmRevert && (keys & (KEY_LEFT | KEY_RIGHT | KEY_A | KEY_X | KEY_Y))) {
            settings.selected = static_cast<unsigned int>(selectedGauge);
            saveSettings(settings);
        }

        if (guiMode) {
            gui.draw(dashboard, frameSamples, settings, liveMode, selectedGauge, confirmRevert,
                     showConnectionError, frameError, frameIp, errorScroll);
        }
        else {
            renderFallback(bottomConsole, dashboard, frameSamples, "citro2d initialization failed");
            drawBottomStatus(bottomConsole, settings.host, settings.port, liveMode, frameError, settings, selectedGauge);
        }
        gspWaitForVBlank();
    }

    net.running = false;
    threadJoin(netThread, U64_MAX);
    threadFree(netThread);
    obd.disconnect();
    settings.selected = static_cast<unsigned int>(selectedGauge);
    saveSettings(settings);
    gui.shutdown();
    gfxExit();
    return 0;
}
