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
    // Optional brand logo from RomFS; failure is non-fatal (falls back to text-only header).
    logoSheet_ = C2D_SpriteSheetLoad("romfs:/gfx/autoui_brand.t3x");
    if (logoSheet_ != nullptr && C2D_SpriteSheetCount(logoSheet_) > 0) {
        logoImage_ = C2D_SpriteSheetGetImage(logoSheet_, 0);
        logoReady_ = true;
    }
    ready_ = true;
    return true;
}

void GuiRenderer::shutdown() {
    if (textBuffer_ != nullptr) {
        C2D_TextBufDelete(textBuffer_);
        textBuffer_ = nullptr;
    }
    if (logoSheet_ != nullptr) {
        C2D_SpriteSheetFree(logoSheet_);
        logoSheet_ = nullptr;
        logoReady_ = false;
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

void GuiRenderer::drawLoading(unsigned int frame) {
    if (!ready_) return;

    C2D_TextBufClear(textBuffer_);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    const u32 background = color(0x0B1220);
    const u32 panel = color(0x111827);
    const u32 accent = color(0x0F766E);
    const u32 foreground = color(0xE5E7EB);
    const u32 muted = color(0x94A3B8);

    C2D_TargetClear(top_, background);
    C2D_SceneBegin(top_);
    C2D_DrawRectSolid(0, 0, 0.0f, 400, 240, background);
    C2D_DrawRectSolid(24, 42, 0.0f, 352, 156, panel);
    C2D_DrawRectSolid(24, 42, 0.0f, 8, 156, accent);
    if (logoReady_) C2D_DrawImageAt(logoImage_, 52.0f, 56.0f, 0.5f, nullptr, 1.18f, 1.18f);
    text("3DS AutoUI", 52, 108, 0.78f, accent);
    text("3DS AutoUI", 52.8f, 108, 0.78f, accent);
    text("STARTING DASHBOARD", 52, 143, 0.58f, foreground);
    text("STARTING DASHBOARD", 52.6f, 143, 0.58f, foreground);
    text("CONNECTING TO OBD ADAPTER", 52, 174, 0.38f, muted);
    const char* dots[] = {".", "..", "..."};
    text(dots[(frame / 12) % 3], 292, 168, 0.76f, accent);

    C2D_TargetClear(bottom_, color(0x0B0F17));
    C2D_SceneBegin(bottom_);
    C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, color(0x0B0F17));
    if (logoReady_) C2D_DrawImageAt(logoImage_, 131.0f, 14.0f, 0.5f, nullptr, 1.80f, 1.80f);
    text("3DS AutoUI", 160, 88, 0.82f, accent, C2D_AlignCenter);
    text("3DS AutoUI", 160.8f, 88, 0.82f, accent, C2D_AlignCenter);
    text("Loading", 160, 130, 0.68f, foreground, C2D_AlignCenter);
    text("Loading", 160.8f, 130, 0.68f, foreground, C2D_AlignCenter);
    text("Please wait", 160, 171, 0.44f, muted, C2D_AlignCenter);
    const unsigned int progress = std::min(frame / 5, 92u);
    char progressText[24];
    snprintf(progressText, sizeof(progressText), "CONNECTING  %u%%", progress);
    text(progressText, 160, 194, 0.30f, muted, C2D_AlignCenter);
    C2D_DrawRectSolid(40, 214, 0.0f, 240, 10, color(0x273449));
    C2D_DrawRectSolid(40, 214, 0.0f, 240.0f * static_cast<float>(progress) / 100.0f, 10, accent);

    C2D_Flush();
    C3D_FrameEnd(0);
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

bool GuiRenderer::sampleValid(const GaugeConfig& gauge, const std::vector<GaugeSample>& samples) const {
    for (const auto& sample : samples) {
        if (sample.id == gauge.id) return sample.valid;
    }
    return false;
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
                       bool confirmRevert, bool showConnectionError, const char* connectionError,
                       const char* localIp, float errorScroll) {
    if (!ready_) return;

    C2D_TextBufClear(textBuffer_);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(top_, color(settings.background));
    C2D_SceneBegin(top_);

    const u32 panel = color(settings.panel);
    const u32 foreground = color(settings.foreground);
    const u32 accent = color(settings.accent);

    C2D_DrawRectSolid(0, 0, 0.0f, 400, 240, color(settings.background));
    if (settings.bannerVisible) {
        const float bw = 384.0f * settings.bannerScale;
        const float bh = 38.0f * settings.bannerScale;
        const float bx = settings.bannerX - bw * 0.5f;
        const float by = settings.bannerY - bh * 0.5f;
        C2D_DrawRectSolid(bx, by, 0.0f, bw, bh, panel);
        C2D_DrawRectSolid(bx, by, 0.0f, 6, bh, accent);
        text(dashboard.brand.c_str(), bx + 14, by + 5, 0.62f * settings.bannerScale, accent);
        text(dashboard.vehicleName.c_str(), bx + 14, by + 21, 0.42f * settings.bannerScale, foreground);
        if (logoReady_) C2D_DrawImageAt(logoImage_, bx + bw - 28, by + 3, 0.5f, nullptr, settings.bannerScale, settings.bannerScale);
        text(liveMode ? "CONNECTED" : "OFFLINE", bx + bw - 64, by + 10, 0.42f * settings.bannerScale, liveMode ? color(0x34D399) : color(0xFBBF24), C2D_AlignRight);
    }

    char valueText[32];
    const auto drawPrimaryGauge = [&](size_t index, const GaugeConfig& gauge, float value, float fraction, bool valid) {
        if (!settings.visible[index]) return;
        if (!valid) fraction = 0.0f;
        const float s = settings.scale[index];
        const float w = 188.0f * s;
        const float h = 78.0f * s;
        const float x = settings.x[index] - w * 0.5f;
        const float y = settings.y[index] - h * 0.5f;
        const u32 gaugeColor = statusColor(value, gauge, settings, index);
        if (!settings.dial[index]) {
            C2D_DrawRectSolid(x, y, 0.0f, w, h, panel);
            text(gauge.label.c_str(), x + 10 * s, y + 8 * s, 0.42f * s, foreground);
            if (valid) snprintf(valueText, sizeof(valueText), "%.0f", value);
            else snprintf(valueText, sizeof(valueText), "--");
            text(valueText, x + 10 * s, y + 24 * s, 1.08f * s, valid ? gaugeColor : color(0x475569));
            text(gauge.unit.c_str(), x + 124 * s, y + 50 * s, 0.40f * s, foreground);
            C2D_DrawRectSolid(x + 10 * s, y + 65 * s, 0.0f, 168.0f * s, 8 * s, color(0x273449));
            C2D_DrawRectSolid(x + 10 * s, y + 65 * s, 0.0f, 168.0f * s * fraction, 8 * s, gaugeColor);
        } else {
            drawDial(x + 92 * s, y + 39 * s, 37 * s, fraction, gaugeColor, foreground, panel);
            text(gauge.label.c_str(), x + 92 * s, y + 1 * s, 0.38f * s, foreground, C2D_AlignCenter);
            if (!valid) text("--", x + 92 * s, y + 27 * s, 0.58f * s, color(0x475569), C2D_AlignCenter);
            drawDialLimits(gauge, x + 92 * s, y + 39 * s, 37 * s, 0.25f * s, foreground);
            text(gauge.unit.c_str(), x + 92 * s, y + 82 * s, 0.38f * s, foreground, C2D_AlignCenter);
        }
    };

    auto drawSecondaryGauge = [&](size_t index) {
        const GaugeConfig& gauge = dashboard.gauges[index];
        if (!settings.visible[index]) return;
        const float s = settings.scale[index];
        const float cardWidth = 92.0f * s;
        const float cardHeight = 43.0f * s;
        const float x = settings.x[index] - cardWidth * 0.5f;
        const float y = settings.y[index] - cardHeight * 0.5f;
        const float value = valueFor(gauge, samples);
        const bool valid = liveMode && sampleValid(gauge, samples);
        const u32 gaugeColor = statusColor(value, gauge, settings, index);
        const float fraction = valid ? std::clamp((value - gauge.min) / (gauge.max - gauge.min), 0.0f, 1.0f) : 0.0f;
        if (!settings.dial[index]) {
            C2D_DrawRectSolid(x, y, 0.0f, cardWidth, cardHeight, panel);
            text(gauge.label.c_str(), x + 6 * s, y + 4 * s, 0.34f * s, foreground);
            if (valid) snprintf(valueText, sizeof(valueText), "%.1f %s", value, gauge.unit.c_str());
            else snprintf(valueText, sizeof(valueText), "-- %s", gauge.unit.c_str());
            text(valueText, x + 6 * s, y + 19 * s, 0.39f * s, valid ? gaugeColor : color(0x475569));
            C2D_DrawRectSolid(x + 6 * s, y + 35 * s, 0.0f, 80 * s, 4 * s, color(0x273449));
            C2D_DrawRectSolid(x + 6 * s, y + 35 * s, 0.0f, 80.0f * s * fraction, 4 * s, gaugeColor);
        } else {
            drawDial(x + 46 * s, y + 25 * s, 17 * s, fraction, gaugeColor, foreground, panel);
            text(gauge.label.c_str(), x + 46 * s, y + 3 * s, 0.27f * s, foreground, C2D_AlignCenter);
            if (!valid) text("--", x + 46 * s, y + 17 * s, 0.34f * s, color(0x475569), C2D_AlignCenter);
            drawDialLimits(gauge, x + 46 * s, y + 25 * s, 17 * s, 0.16f * s, foreground);
            text(gauge.unit.c_str(), x + 46 * s, y + 42 * s, 0.28f * s, foreground, C2D_AlignCenter);
        }
    };

    // Draw non-selected gauges first, then selected last (on top)
    for (size_t i = 0; i < 2; ++i) {
        if (i == selectedGauge) continue;
        drawPrimaryGauge(i, dashboard.gauges[i], valueFor(dashboard.gauges[i], samples),
            std::clamp((valueFor(dashboard.gauges[i], samples) - dashboard.gauges[i].min) /
            (dashboard.gauges[i].max - dashboard.gauges[i].min), 0.0f, 1.0f),
            liveMode && sampleValid(dashboard.gauges[i], samples));
    }
    for (size_t index = 2; index < dashboard.gauges.size(); ++index) {
        if (index == selectedGauge) continue;
        drawSecondaryGauge(index);
    }
    // Selected gauge drawn last
    if (selectedGauge < 2) {
        const auto& g = dashboard.gauges[selectedGauge];
        drawPrimaryGauge(selectedGauge, g, valueFor(g, samples),
            std::clamp((valueFor(g, samples) - g.min) / (g.max - g.min), 0.0f, 1.0f),
            liveMode && sampleValid(g, samples));
    } else if (selectedGauge < dashboard.gauges.size()) {
        drawSecondaryGauge(selectedGauge);
    }

    // Bottom screen: show the error takeover while disconnected, otherwise the normal editor.
    const bool hasError = showConnectionError && !liveMode && connectionError != nullptr && connectionError[0] != '\0';
    if (hasError) {
        drawConnectionError(connectionError, localIp, settings.host, settings.port, errorScroll);
    } else {
        drawSettings(dashboard, settings, selectedGauge, liveMode, confirmRevert);
    }

    C2D_Flush();
    C3D_FrameEnd(0);
}

void GuiRenderer::drawConnectionError(const char* connectionError, const char* localIp, const char* targetHost,
                                      unsigned int targetPort, float scrollOffset) {
    if (!ready_) return;

    C2D_TargetClear(bottom_, color(0x0B0F17));
    C2D_SceneBegin(bottom_);

    const u32 errorRed = rgba(0xEF4444);
    const u32 accent = rgba(0x60A5FA);
    const u32 foreground = rgba(0xE5E7EB);

    C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, color(0x0B0F17));
    C2D_DrawRectSolid(8, 8, 0.0f, 304, 28, rgba(0x1F2937));
    text("X", 16, 14, 0.52f, errorRed);
    text("CONNECTION ERROR", 34, 16, 0.56f, errorRed);
    text("BUILD " __DATE__ " " __TIME__, 306, 4, 0.38f, rgba(0x94A3B8), C2D_AlignRight);

    // Scrollable message area, so long diagnostics never get silently cut off.
    const std::vector<std::string> lines = wrapText(connectionError, 20);
    const float lineHeight = 32.0f;
    const float viewTop = 50.0f;
    const float viewBottom = 170.0f;
    const float contentHeight = static_cast<float>(lines.size()) * lineHeight;
    const float maxScroll = std::max(0.0f, contentHeight - (viewBottom - viewTop));
    const float clampedScroll = std::clamp(scrollOffset, 0.0f, maxScroll);

    for (size_t i = 0; i < lines.size(); ++i) {
        const float y = viewTop + static_cast<float>(i) * lineHeight - clampedScroll;
        if (y + lineHeight < viewTop || y > viewBottom) continue;
        text(lines[i].c_str(), 14, y, 0.52f, foreground);
    }
    if (maxScroll > 0.0f) {
        if (clampedScroll > 0.0f) text("^ MORE ABOVE", 210, viewTop, 0.30f, accent);
        if (clampedScroll < maxScroll) text("v MORE BELOW", 210, viewBottom - 12, 0.30f, accent);
    }

    float cursorY = viewBottom + 12.0f;
    if (localIp != nullptr && localIp[0] != '\0') {
        char ipLine[48];
        snprintf(ipLine, sizeof(ipLine), "3DS IP: %s", localIp);
        text(ipLine, 14, cursorY, 0.46f, accent);
        cursorY += 26.0f;
    }
    char targetLine[48];
    snprintf(targetLine, sizeof(targetLine), "TARGET: %s:%u", targetHost, targetPort);
    text(targetLine, 14, cursorY, 0.34f, foreground);
    cursorY += 22.0f;
    text("B RETRY | Y EDIT IP | UP/DOWN SCROLL", 14, cursorY, 0.28f, accent);
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
    const u32 borderGray = color(0x374151);

    C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, color(0x0B0F17));
    C2D_DrawRectSolid(8, 8, 0.0f, 304, 28, panel);
    text("CUSTOMIZE DASHBOARD", 16, 14, 0.46f, foreground);
    text(liveMode ? "LIVE" : "OFFLINE", 244, 14, 0.32f, liveMode ? color(0x34D399) : color(0xFBBF24), C2D_AlignRight);

    if (confirmRevert) {
        C2D_DrawRectSolid(24, 68, 0.0f, 272, 112, panel);
        text("REVERT ALL DEFAULTS?", 52, 84, 0.52f, accent);
        text("This resets colors, visibility", 42, 112, 0.34f, foreground);
        text("and gauge positions.", 78, 130, 0.34f, foreground);
        text("A  CONFIRM", 54, 158, 0.40f, color(0x34D399));
        text("B  CANCEL", 198, 158, 0.40f, color(0xEF4444));
        return;
    }

    text(settings.editMode ? "DONE" : "EDIT", 162, 14, 0.36f, accent);
    text("RESET", 302, 14, 0.36f, color(0xFBBF24), C2D_AlignRight);

    if (settings.editMode) {
        // Proportional minimap: scale the 400x240 top screen into a ~300x140 edit area
        constexpr float EDIT_SCALE = 0.58f;
        constexpr float EDIT_OFFSET_X = 10.0f;
        constexpr float EDIT_OFFSET_Y = 42.0f;

        // Draw non-selected gauge boxes first, then selected on top
        auto drawGaugeBox = [&](size_t index) {
            const float s = settings.scale[index];
            const float baseW = index < 2 ? 188.0f : 92.0f;
            const float baseH = index < 2 ? 78.0f : 43.0f;
            const float w = baseW * s * EDIT_SCALE;
            const float h = baseH * s * EDIT_SCALE;
            const float x = settings.x[index] * EDIT_SCALE + EDIT_OFFSET_X - w * 0.5f;
            const float y = settings.y[index] * EDIT_SCALE + EDIT_OFFSET_Y - h * 0.5f;
            C2D_DrawRectSolid(x, y, 0.0f, w, h, settings.visible[index] ? panel : color(0x252A33));
            text(dashboard.gauges[index].label.c_str(), x + 4, y + 3, 0.28f, foreground);
            C2D_DrawRectSolid(x, y, 0.1f, w, 1, borderGray);
            C2D_DrawRectSolid(x, y + h - 1, 0.1f, w, 1, borderGray);
            C2D_DrawRectSolid(x, y, 0.1f, 1, h, borderGray);
            C2D_DrawRectSolid(x + w - 1, y, 0.1f, 1, h, borderGray);
            if (index == selectedGauge && !settings.bannerSelected)
                drawSelectionBorder(x, y, w, h);
        };

        auto drawBannerBox = [&]() {
            const float bw = 384.0f * settings.bannerScale * EDIT_SCALE;
            const float bh = 38.0f * settings.bannerScale * EDIT_SCALE;
            const float bx = settings.bannerX * EDIT_SCALE + EDIT_OFFSET_X - bw * 0.5f;
            const float by = settings.bannerY * EDIT_SCALE + EDIT_OFFSET_Y - bh * 0.5f;
            C2D_DrawRectSolid(bx, by, 0.0f, bw, bh, settings.bannerVisible ? panel : color(0x252A33));
            text("BANNER", bx + 4, by + 4, 0.30f, accent);
            C2D_DrawRectSolid(bx, by, 0.1f, bw, 1, borderGray);
            C2D_DrawRectSolid(bx, by + bh - 1, 0.1f, bw, 1, borderGray);
            C2D_DrawRectSolid(bx, by, 0.1f, 1, bh, borderGray);
            C2D_DrawRectSolid(bx + bw - 1, by, 0.1f, 1, bh, borderGray);
            if (settings.bannerSelected) drawSelectionBorder(bx, by, bw, bh);
        };

        // Draw non-selected items first, selected item last (on top)
        if (!settings.bannerSelected) drawBannerBox();
        for (size_t index = 0; index < dashboard.gauges.size(); ++index) {
            if (index == selectedGauge && !settings.bannerSelected) continue;
            if (settings.bannerSelected || index != selectedGauge) drawGaugeBox(index);
        }
        // Draw selected item on top
        if (settings.bannerSelected) drawBannerBox();
        else drawGaugeBox(selectedGauge);

        // --- Toolbar at bottom (y=190 to y=240) ---
        C2D_DrawRectSolid(0, 188, 0.0f, 320, 1, borderGray);

        // Scale slider
        C2D_DrawRectSolid(8, 192, 0.0f, 18, 18, color(0x273449));
        text("-", 17, 192, 0.62f, foreground, C2D_AlignCenter);
        C2D_DrawRectSolid(30, 200, 0.0f, 96, 4, color(0x273449));
        float currentScale = settings.bannerSelected ? settings.bannerScale : settings.scale[selectedGauge];
        float sliderFill = (currentScale - 0.5f) / 1.5f;
        if (sliderFill < 0.0f) sliderFill = 0.0f;
        if (sliderFill > 1.0f) sliderFill = 1.0f;
        C2D_DrawRectSolid(30, 200, 0.1f, 96.0f * sliderFill, 4, accent);
        C2D_DrawRectSolid(128, 192, 0.0f, 18, 18, color(0x273449));
        text("+", 137, 192, 0.62f, foreground, C2D_AlignCenter);
        char scaleText[8];
        snprintf(scaleText, sizeof(scaleText), "%.1fx", currentScale);
        text(scaleText, 150, 194, 0.30f, foreground);

        // Visibility toggle button
        bool isVisible = settings.bannerSelected ? settings.bannerVisible : settings.visible[selectedGauge];
        C2D_DrawRectSolid(174, 192, 0.0f, 60, 18, isVisible ? color(0x34D399) : color(0x4B5563));
        text(isVisible ? "VISIBLE" : "HIDDEN", 204, 195, 0.28f, color(0x0B0F17), C2D_AlignCenter);

        // Gauge style toggle button (not applicable to banner)
        if (!settings.bannerSelected) {
            bool isDial = settings.dial[selectedGauge];
            C2D_DrawRectSolid(240, 192, 0.0f, 60, 18, isDial ? accent : color(0x4B5563));
            text(isDial ? "DIAL" : "BAR", 270, 195, 0.28f, color(0x0B0F17), C2D_AlignCenter);
        }

        text("DRAG TO MOVE | L/R SELECT", 36, 224, 0.26f, foreground);
        return;
    }

    // Non-edit mode: gauge list + color picker (no dial toggle or visibility toggle here)
    text("SELECT GAUGE", 12, 43, 0.34f, foreground);
    for (size_t index = 0; index < dashboard.gauges.size(); ++index) {
        const float x = 10.0f + static_cast<float>(index % 2) * 154.0f;
        const float y = 56.0f + static_cast<float>(index / 2) * 21.0f;
        const bool active = index == selectedGauge;
        C2D_DrawRectSolid(x, y, 0.0f, 146, 19, active ? accent : panel);
        text(dashboard.gauges[index].label.c_str(), x + 6, y + 3, 0.32f, foreground);
        C2D_DrawRectSolid(x + 130, y + 4, 0.0f, 8, 11, color(settings.gaugeColor[index]));
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
    text("TOUCH RGB TO ADJUST", 94, 229, 0.28f, foreground);
}
