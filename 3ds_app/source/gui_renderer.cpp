#include "gui_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

u32 rgba(unsigned int rgb) {
    return C2D_Color32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0xFF);
}

void drawSelectionBorder(float x, float y, float width, float height) {
    const u32 yellow = rgba(0xFBBF24);
    C2D_DrawRectSolid(x, y, 0.0f, width, 2, yellow);
    C2D_DrawRectSolid(x, y + height - 2, 0.0f, width, 2, yellow);
    C2D_DrawRectSolid(x, y, 0.0f, 2, height, yellow);
    C2D_DrawRectSolid(x + width - 2, y, 0.0f, 2, height, yellow);
}

void drawDial(float centerX, float centerY, float radius, float fraction, u32 dialColor, u32 foreground,
              u32 panel) {
    constexpr float pi = 3.14159265f;
    constexpr float startAngle = 0.75f * pi;
    constexpr float sweepAngle = 1.5f * pi;
    const float angle = startAngle + sweepAngle * std::clamp(fraction, 0.0f, 1.0f);

    C2D_DrawCircleSolid(centerX, centerY, 0.0f, radius, rgba(0x273449));
    C2D_DrawCircleSolid(centerX, centerY, 0.1f, radius - 3.0f, panel);
    for (int tick = 0; tick <= 10; ++tick) {
        const float tickAngle = startAngle + sweepAngle * static_cast<float>(tick) / 10.0f;
        const float outerRadius = radius - 2.0f;
        const float innerRadius = radius - (tick % 2 == 0 ? 7.0f : 4.0f);
        C2D_DrawLine(centerX + std::cos(tickAngle) * innerRadius, centerY + std::sin(tickAngle) * innerRadius,
                     foreground, centerX + std::cos(tickAngle) * outerRadius, centerY + std::sin(tickAngle) * outerRadius,
                     foreground, 1.0f, 0.2f);
    }
    C2D_DrawLine(centerX, centerY, dialColor, centerX + std::cos(angle) * (radius - 8.0f),
                 centerY + std::sin(angle) * (radius - 8.0f), dialColor, 2.0f, 0.3f);
    C2D_DrawCircleSolid(centerX, centerY, 0.4f, 3.0f, dialColor);
}

std::vector<std::string> wrapText(const std::string& text, size_t maxCharsPerLine) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.length()) {
        size_t end = std::min(start + maxCharsPerLine, text.length());
        if (end < text.length()) {
            size_t lastSpace = text.rfind(' ', end);
            if (lastSpace > start) {
                end = lastSpace;
            }
        }
        lines.push_back(text.substr(start, end - start));
        start = end;
        while (start < text.length() && text[start] == ' ') ++start;
    }
    return lines;
}

} // namespace

bool GuiRenderer::init() {
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) return false;
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        C3D_Fini();
        return false;
    }
    C2D_Prepare();
    top_ = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom_ = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    textBuffer_ = C2D_TextBufNew(4096);
    if (top_ == nullptr || bottom_ == nullptr || textBuffer_ == nullptr) {
        shutdown();
        return false;
    }
    ready_ = true;
    return true;
}

void GuiRenderer::shutdown() {
    if (textBuffer_ != nullptr) {
        C2D_TextBufDelete(textBuffer_);
        textBuffer_ = nullptr;
    }
    if (top_ != nullptr) top_ = nullptr;
    if (ready_) {
        C2D_Fini();
        C3D_Fini();
        ready_ = false;
    }
}

bool GuiRenderer::ready() const {
    return ready_;
}

u32 GuiRenderer::color(unsigned int rgb) const {
    return rgba(rgb);
}

u32 GuiRenderer::statusColor(float value, const GaugeConfig& gauge, const GuiSettings& settings, size_t index) const {
    if (value >= gauge.critical) return color(0xEF4444);
    if (value >= gauge.warning) return color(0xFBBF24);
    return color(settings.gaugeColor[index]);
}

float GuiRenderer::valueFor(const GaugeConfig& gauge, const std::vector<GaugeSample>& samples) const {
    for (const auto& sample : samples) {
        if (sample.id == gauge.id) return sample.value;
    }
    return 0.0f;
}

void GuiRenderer::text(const char* value, float x, float y, float scale, u32 textColor, u32 flags) {
    C2D_Text parsed;
    if (C2D_TextParse(&parsed, textBuffer_, value) != nullptr) {
        C2D_DrawText(&parsed, flags | C2D_WithColor, x, y, 0.1f, scale, scale, textColor);
    }
}

void GuiRenderer::drawDialLimits(const GaugeConfig& gauge, float centerX, float centerY, float radius,
                                 float scale, u32 textColor) {
    char limitText[16];
    const float labelY = centerY + radius * 0.55f;
    snprintf(limitText, sizeof(limitText), "%.0f", gauge.min);
    text(limitText, centerX - radius * 0.53f, labelY, scale, textColor, C2D_AlignCenter);
    snprintf(limitText, sizeof(limitText), "%.0f", gauge.max);
    text(limitText, centerX + radius * 0.53f, labelY, scale, textColor, C2D_AlignCenter);
}

void GuiRenderer::draw(const DashboardData& dashboard, const std::vector<GaugeSample>& samples,
                       const GuiSettings& settings, bool liveMode, size_t selectedGauge,
                       bool confirmRevert, const char* connectionError, const char* localIp) {
    if (!ready_) return;

    C2D_TextBufClear(textBuffer_);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(top_, color(settings.background));
    C2D_SceneBegin(top_);

    const u32 panel = color(settings.panel);
    const u32 foreground = color(settings.foreground);
    const u32 accent = color(settings.accent);

    C2D_DrawRectSolid(0, 0, 0.0f, 400, 240, color(settings.background));
    C2D_DrawRectSolid(8, 8, 0.0f, 384, 38, panel);
    C2D_DrawRectSolid(8, 8, 0.0f, 6, 38, accent);
    text(dashboard.brand.c_str(), 22, 13, 0.62f, accent);
    text(dashboard.vehicleName.c_str(), 22, 29, 0.42f, foreground);
    text(liveMode ? "CONNECTED" : "OFFLINE", 320, 18, 0.42f, liveMode ? color(0x34D399) : color(0xFBBF24), C2D_AlignRight);

    const GaugeConfig& rpm = dashboard.gauges[0];
    const GaugeConfig& speed = dashboard.gauges[1];
    const float rpmValue = valueFor(rpm, samples);
    const float speedValue = valueFor(speed, samples);
    
        if (settings.selected == 0 && settings.visible[0]) drawSelectionBorder(settings.x[0], settings.y[0], 188, 78);
        if (settings.selected == 1 && settings.visible[1]) drawSelectionBorder(settings.x[1], settings.y[1], 188, 78);

    char valueText[32];
    const float rpmFraction = std::clamp((rpmValue - rpm.min) / (rpm.max - rpm.min), 0.0f, 1.0f);
    const float speedFraction = std::clamp((speedValue - speed.min) / (speed.max - speed.min), 0.0f, 1.0f);
    const auto drawPrimaryGauge = [&](size_t index, const GaugeConfig& gauge, float value, float fraction) {
        if (!settings.visible[index]) return;
        const float x = settings.x[index];
        const float y = settings.y[index];
        const u32 gaugeColor = statusColor(value, gauge, settings, index);
        if (!settings.dial[index]) {
            C2D_DrawRectSolid(x, y, 0.0f, 188, 78, panel);
            text(gauge.label.c_str(), x + 10, y + 8, 0.42f, foreground);
            snprintf(valueText, sizeof(valueText), "%.0f", value);
            text(valueText, x + 10, y + 24, 1.08f, gaugeColor);
            text(gauge.unit.c_str(), x + 124, y + 50, 0.40f, foreground);
            C2D_DrawRectSolid(x + 10, y + 65, 0.0f, 168, 8, color(0x273449));
            C2D_DrawRectSolid(x + 10, y + 65, 0.0f, 168.0f * fraction, 8, gaugeColor);
        } else {
            drawDial(x + 92, y + 39, 37, fraction, gaugeColor, foreground, panel);
            text(gauge.label.c_str(), x + 92, y + 1, 0.38f, foreground, C2D_AlignCenter);
            drawDialLimits(gauge, x + 92, y + 39, 37, 0.25f, foreground);
            text(gauge.unit.c_str(), x + 92, y + 82, 0.38f, foreground, C2D_AlignCenter);
        }
    };
    drawPrimaryGauge(0, rpm, rpmValue, rpmFraction);
    drawPrimaryGauge(1, speed, speedValue, speedFraction);

    const float cardWidth = 92.0f;
    const float cardHeight = 43.0f;
    for (size_t index = 2; index < dashboard.gauges.size(); ++index) {
        const GaugeConfig& gauge = dashboard.gauges[index];
        if (!settings.visible[index]) continue;
        const float x = settings.x[index];
        const float y = settings.y[index];
        const float value = valueFor(gauge, samples);
        const u32 gaugeColor = statusColor(value, gauge, settings, index);
        const float fraction = std::clamp((value - gauge.min) / (gauge.max - gauge.min), 0.0f, 1.0f);
        if (!settings.dial[index]) {
            C2D_DrawRectSolid(x, y, 0.0f, cardWidth, cardHeight, panel);
            text(gauge.label.c_str(), x + 6, y + 4, 0.34f, foreground);
            snprintf(valueText, sizeof(valueText), "%.1f %s", value, gauge.unit.c_str());
            text(valueText, x + 6, y + 19, 0.39f, gaugeColor);
            C2D_DrawRectSolid(x + 6, y + 35, 0.0f, 80, 4, color(0x273449));
            C2D_DrawRectSolid(x + 6, y + 35, 0.0f, 80.0f * fraction, 4, gaugeColor);
        } else {
            drawDial(x + 46, y + 25, 17, fraction, gaugeColor, foreground, panel);
            text(gauge.label.c_str(), x + 46, y + 3, 0.27f, foreground, C2D_AlignCenter);
            drawDialLimits(gauge, x + 46, y + 25, 17, 0.16f, foreground);
            text(gauge.unit.c_str(), x + 46, y + 42, 0.28f, foreground, C2D_AlignCenter);
                if (settings.selected == index) drawSelectionBorder(x + 10, y + 2, 72, 40);
        }
    }

    // Bottom screen: show the error takeover while disconnected, otherwise the normal editor.
    const bool hasError = !liveMode && connectionError != nullptr && connectionError[0] != '\0';
    if (hasError) {
        drawConnectionError(connectionError, localIp, settings.host, settings.port);
    } else {
        drawSettings(dashboard, settings, selectedGauge, liveMode, confirmRevert);
    }

    C2D_Flush();
    C3D_FrameEnd(0);
}

void GuiRenderer::drawConnectionError(const char* connectionError, const char* localIp, const char* targetHost,
                                      unsigned int targetPort) {
    if (!ready_) return;

    C2D_TargetClear(bottom_, color(0x0B0F17));
    C2D_SceneBegin(bottom_);

    const u32 errorRed = rgba(0xEF4444);
    const u32 accent = rgba(0x60A5FA);
    const u32 foreground = rgba(0xE5E7EB);

    C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, color(0x0B0F17));
    C2D_DrawRectSolid(8, 8, 0.0f, 304, 28, rgba(0x1F2937));
    text("CONNECTION ERROR", 16, 16, 0.56f, errorRed);
    text("BUILD " __DATE__ " " __TIME__, 306, 4, 0.38f, rgba(0x94A3B8), C2D_AlignRight);

    const std::vector<std::string> lines = wrapText(connectionError, 20);
    float cursorY = 60.0f;
    for (size_t i = 0; i < lines.size() && i < 5; ++i) {
        text(lines[i].c_str(), 14, cursorY, 0.52f, foreground);
        cursorY += 32.0f;
    }
    cursorY += 10.0f;

    if (localIp != nullptr && localIp[0] != '\0') {
        char ipLine[48];
        snprintf(ipLine, sizeof(ipLine), "3DS IP: %s", localIp);
        text(ipLine, 14, cursorY, 0.46f, accent);
        cursorY += 30.0f;
    }
    char targetLine[48];
    snprintf(targetLine, sizeof(targetLine), "TARGET: %s:%u", targetHost, targetPort);
    text(targetLine, 14, cursorY, 0.38f, foreground);
    cursorY += 30.0f;
    text("PRESS B TO RETRY  |  Y TO EDIT IP", 14, cursorY, 0.34f, accent);
}

void GuiRenderer::drawSettings(const DashboardData& dashboard, const GuiSettings& settings,
                               size_t selectedGauge, bool liveMode, bool confirmRevert) {
    if (!ready_) return;

    (void)confirmRevert;

    C2D_TargetClear(bottom_, color(0x0B0F17));
    C2D_SceneBegin(bottom_);

    const u32 foreground = color(settings.foreground);
    const u32 panel = color(settings.panel);
    const u32 accent = color(settings.accent);
    const unsigned int selectedColor = settings.gaugeColor[selectedGauge];

    C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, color(0x0B0F17));
    C2D_DrawRectSolid(8, 8, 0.0f, 304, 28, panel);
    text("CUSTOMIZE DASHBOARD", 16, 14, 0.46f, foreground);
    text(liveMode ? "LIVE" : "OFFLINE", 254, 14, 0.34f, liveMode ? color(0x34D399) : color(0xFBBF24));

    if (confirmRevert) {
        C2D_DrawRectSolid(24, 68, 0.0f, 272, 112, panel);
        text("REVERT ALL DEFAULTS?", 52, 84, 0.52f, accent);
        text("This resets colors, visibility", 42, 112, 0.34f, foreground);
        text("and gauge positions.", 78, 130, 0.34f, foreground);
        text("A  CONFIRM", 54, 158, 0.40f, color(0x34D399));
        text("B  CANCEL", 198, 158, 0.40f, color(0xEF4444));
        return;
    }

    text(settings.editMode ? "DONE" : "EDIT", 204, 14, 0.36f, accent);
    text("RESET", 258, 14, 0.36f, color(0xFBBF24));

    if (settings.editMode) {
        text("EDIT MODE - DRAG GAUGES", 12, 43, 0.38f, accent);
        for (size_t index = 0; index < dashboard.gauges.size(); ++index) {
            const float x = settings.x[index] * 0.78f;
            const float y = settings.y[index];
            const float width = index < 2 ? 110.0f : 90.0f;
            const float height = index < 2 ? 48.0f : 34.0f;
            C2D_DrawRectSolid(x, y, 0.0f, width, height, settings.visible[index] ? panel : color(0x252A33));
            text(dashboard.gauges[index].label.c_str(), x + 6, y + 7, index < 2 ? 0.48f : 0.42f, foreground);
        }
        text("Touch and drag  |  A toggle visibility", 36, 217, 0.30f, foreground);
        return;
    }
    text("TAP DIAL BOX FOR GAUGE STYLE", 12, 148, 0.31f, accent);

    text("SELECT GAUGE", 12, 43, 0.34f, foreground);
    for (size_t index = 0; index < dashboard.gauges.size(); ++index) {
        const float x = 10.0f + static_cast<float>(index % 2) * 154.0f;
        const float y = 58.0f + static_cast<float>(index / 2) * 25.0f;
        const bool active = index == selectedGauge;
        C2D_DrawRectSolid(x, y, 0.0f, 146, 21, active ? accent : panel);
        text(dashboard.gauges[index].label.c_str(), x + 6, y + 4, 0.34f, foreground);
        C2D_DrawRectSolid(x + 91, y + 5, 0.0f, 11, 11, foreground);
        C2D_DrawRectSolid(x + 93, y + 7, 0.1f, 7, 7, panel);
        if (settings.dial[index]) C2D_DrawRectSolid(x + 94, y + 8, 0.2f, 5, 5, accent);
        text("DIAL", x + 105, y + 5, 0.20f, foreground);
        C2D_DrawRectSolid(x + 130, y + 5, 0.0f, 8, 11, color(settings.gaugeColor[index]));
    }

    text("RGB COLOR", 12, 165, 0.36f, foreground);
    char value[24];
    unsigned int red = (selectedColor >> 16) & 0xFF;
    unsigned int green = (selectedColor >> 8) & 0xFF;
    unsigned int blue = selectedColor & 0xFF;
    const unsigned int channels[] = {red, green, blue};
    const u32 channelColors[] = {color(0xEF4444), color(0x34D399), color(0x60A5FA)};
    const char* channelNames[] = {"R", "G", "B"};
    for (unsigned int channel = 0; channel < 3; ++channel) {
        const float y = 179.0f + static_cast<float>(channel) * 17.0f;
        text(channelNames[channel], 12, y, 0.34f, channelColors[channel]);
        C2D_DrawRectSolid(28, y + 3, 0.0f, 220, 8, color(0x273449));
        C2D_DrawRectSolid(28, y + 3, 0.0f, 220.0f * channels[channel] / 255.0f, 8, channelColors[channel]);
        snprintf(value, sizeof(value), "%03u", channels[channel]);
        text(value, 258, y, 0.34f, foreground);
    }
    snprintf(value, sizeof(value), "#%06X", selectedColor);
    text(value, 12, 229, 0.36f, accent);
    text("A VISIBILITY  |  TOUCH RGB", 94, 229, 0.28f, foreground);
}
