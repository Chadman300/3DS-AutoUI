#pragma once

#include <3ds.h>
#include <citro2d.h>

#include <vector>

#include "dashboard.hpp"

struct GuiSettings {
    unsigned int background = 0x0B1220;
    unsigned int panel = 0x111827;
    unsigned int accent = 0xFF5A36;
    unsigned int foreground = 0xE5E7EB;
    unsigned int selected = 0;
    unsigned int theme = 0;
    bool editMode = false;
    bool visible[10];
    bool dial[10];
    unsigned int gaugeColor[10];
    float x[10];
    float y[10];
    char host[16];
    unsigned int port = 35000;

    GuiSettings() {
        // Gauges 0-1 are the large top cards; 2-9 form a 4-column x 2-row grid below.
        const float defaultX[10] = {8, 204, 8, 106, 204, 302, 8, 106, 204, 302};
        const float defaultY[10] = {54, 54, 140, 140, 140, 140, 190, 190, 190, 190};
        for (unsigned int index = 0; index < 10; ++index) {
            visible[index] = true;
            dial[index] = false;
            const unsigned int defaults[10] = {0xFF5A36, 0x60A5FA, 0xFBBF24, 0x34D399, 0xA78BFA,
                                               0xF472B6, 0x22D3EE, 0xFCD34D, 0xF97316, 0x38BDF8};
            gaugeColor[index] = defaults[index];
            x[index] = defaultX[index];
            y[index] = defaultY[index];
        }
        const char defaultHost[] = "192.168.4.1";
        for (unsigned int i = 0; i < sizeof(defaultHost); ++i) host[i] = defaultHost[i];
    }
};

class GuiRenderer {
public:
    bool init();
    void shutdown();
    bool ready() const;
    void draw(const DashboardData& dashboard, const std::vector<GaugeSample>& samples,
              const GuiSettings& settings, bool liveMode, size_t selectedGauge, bool confirmRevert,
              const char* connectionError = "", const char* localIp = "", float errorScroll = 0.0f);
    void drawSettings(const DashboardData& dashboard, const GuiSettings& settings,
                      size_t selectedGauge, bool liveMode, bool confirmRevert);
    void drawConnectionError(const char* connectionError, const char* localIp, const char* targetHost,
                             unsigned int targetPort, float scrollOffset);

private:
    void text(const char* value, float x, float y, float scale, u32 color, u32 flags = 0);
    float valueFor(const GaugeConfig& gauge, const std::vector<GaugeSample>& samples) const;
    bool sampleValid(const GaugeConfig& gauge, const std::vector<GaugeSample>& samples) const;
    u32 color(unsigned int rgb) const;
    u32 statusColor(float value, const GaugeConfig& gauge, const GuiSettings& settings, size_t index) const;
    void drawDialLimits(const GaugeConfig& gauge, float centerX, float centerY, float radius,
                        float scale, u32 textColor);

    C3D_RenderTarget* top_ = nullptr;
    C3D_RenderTarget* bottom_ = nullptr;
    C2D_TextBuf textBuffer_ = nullptr;
    C2D_SpriteSheet logoSheet_ = nullptr;
    C2D_Image logoImage_{};
    bool logoReady_ = false;
    bool ready_ = false;
};
