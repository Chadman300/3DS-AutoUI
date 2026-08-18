#pragma once

#include "gui_renderer.hpp"
#include <cstdio>
#include <string>
#include <sys/stat.h>

// Persists settings (including the OBD adapter's IP) as a raw struct dump on the SD card,
// so the adapter host can be changed without rebuilding the app.
inline const char* settingsPath() {
    return "sdmc:/3ds/3ds_autoui/settings.bin";
}

inline void loadSettings(GuiSettings& settings) {
    FILE* file = fopen(settingsPath(), "rb");
    if (file == nullptr) return;
    GuiSettings loaded;
    if (fread(&loaded, sizeof(GuiSettings), 1, file) == 1) {
        settings = loaded;
    }
    fclose(file);
}

inline void saveSettings(const GuiSettings& settings) {
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/3ds_autoui", 0777);
    FILE* file = fopen(settingsPath(), "wb");
    if (file == nullptr) return;
    fwrite(&settings, sizeof(GuiSettings), 1, file);
    fclose(file);
}
