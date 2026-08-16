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
    bool visible[8];
    bool dial[8];
    unsigned int gaugeColor[8];
    float x[8];
    float y[8];

    GuiSettings() {
        const float defaultX[8] = {8, 204, 8, 106, 204, 8, 106, 204};
        const float defaultY[8] = {54, 54, 140, 140, 140, 190, 190, 190};
        for (unsigned int index = 0; index < 8; ++index) {
            visible[index] = true;
            dial[index] = false;
            const unsigned int defaults[8] = {0xFF5A36, 0x60A5FA, 0xFBBF24, 0x34D399, 0xA78BFA, 0xF472B6, 0x22D3EE, 0xFCD34D};
            gaugeColor[index] = defaults[index];
            x[index] = defaultX[index];
            y[index] = defaultY[index];
        }
    }
};

class GuiRenderer {
public:
    bool init();
    void shutdown();
    bool ready() const;
    void draw(const DashboardData& dashboard, const std::vector<GaugeSample>& samples,
              const GuiSettings& settings, bool liveMode, size_t selectedGauge, bool confirmRevert,
              const char* connectionError = "", const char* localIp = "");
    void drawSettings(const DashboardData& dashboard, const GuiSettings& settings,
                      size_t selectedGauge, bool liveMode, bool confirmRevert);
    void drawConnectionError(const char* connectionError, const char* localIp);

private:
    void text(const char* value, float x, float y, float scale, u32 color, u32 flags = 0);
    float valueFor(const GaugeConfig& gauge, const std::vector<GaugeSample>& samples) const;
    u32 color(unsigned int rgb) const;
    u32 statusColor(float value, const GaugeConfig& gauge, const GuiSettings& settings, size_t index) const;
    void drawDialLimits(const GaugeConfig& gauge, float centerX, float centerY, float radius,
                        float scale, u32 textColor);

    C3D_RenderTarget* top_ = nullptr;
    C3D_RenderTarget* bottom_ = nullptr;
    C2D_TextBuf textBuffer_ = nullptr;
    bool ready_ = false;
};
