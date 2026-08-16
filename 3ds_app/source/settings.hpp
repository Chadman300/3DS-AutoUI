#pragma once

#include "gui_renderer.hpp"
#include <cstdio>
#include <string>

// Stub implementations for settings persistence.
// In a full implementation, these would save/load from SD card.

inline void loadSettings(GuiSettings& settings) {
    // TODO: Load from SD card
    // For now, just use defaults (set in GuiSettings constructor)
}

inline void saveSettings(const GuiSettings& settings) {
    // TODO: Save to SD card
    // For now, just no-op
}
