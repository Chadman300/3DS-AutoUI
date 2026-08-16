#pragma once

#include <algorithm>
#include <string>
#include <vector>

struct GaugeConfig {
    std::string id;
    std::string label;
    std::string unit;
    float min;
    float max;
    float warning;
    float critical;
    unsigned int color;
    bool enabled = true;
};

struct ThemeConfig {
    unsigned int background;
    unsigned int panel;
    unsigned int foreground;
    unsigned int accent;
    unsigned int warning;
    unsigned int critical;
    unsigned int success;
    unsigned int secondary;
};

struct GaugeSample {
    std::string id;
    float value;
};

struct DashboardData {
    std::string vehicleName;
    std::string brand;
    ThemeConfig theme;
    std::vector<GaugeConfig> gauges;
};

inline float clampf(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

inline unsigned int statusColorFor(float value, const GaugeConfig& gauge, const ThemeConfig& theme) {
    if (value >= gauge.critical) {
        return theme.critical;
    }
    if (value >= gauge.warning) {
        return theme.warning;
    }
    return gauge.color;
}

inline DashboardData makeWrxDashboard() {
    DashboardData d;
    d.vehicleName = "Subaru WRX 2010";
    d.brand = "SUBARU";
    d.theme = {
        0x0B1220, // background
        0x111827, // panel
        0xE5E7EB, // foreground
        0xFF5A36, // accent
        0xFBBF24, // warning
        0xEF4444, // critical
        0x34D399, // success
        0x60A5FA  // secondary
    };

    d.gauges = {
        {"rpm", "RPM", "RPM", 0.0f, 8000.0f, 6500.0f, 7200.0f, 0xFF5A36, true},
        {"speed", "Speed", "MPH", 0.0f, 140.0f, 110.0f, 130.0f, 0x60A5FA, true},
        {"coolant_temp", "Coolant", "C", 0.0f, 130.0f, 95.0f, 110.0f, 0xFBBF24, true},
        {"boost", "Boost", "psi", -5.0f, 25.0f, 18.0f, 22.0f, 0x34D399, true},
        {"battery_voltage", "Voltage", "V", 10.0f, 15.0f, 11.5f, 10.5f, 0xA78BFA, true},
        {"throttle_position", "Throttle", "%", 0.0f, 100.0f, 85.0f, 95.0f, 0xF472B6, true},
        {"engine_load", "Load", "%", 0.0f, 100.0f, 80.0f, 95.0f, 0x22D3EE, true},
        {"fuel_level", "Fuel", "%", 0.0f, 100.0f, 15.0f, 8.0f, 0xFCD34D, true}
    };

    return d;
}
