#pragma once

#include <windows.h>
#include <string>
#include <vector>

// One connected display, as seen by the screensaver. Rects are in virtual-screen
// pixels (the process is per-monitor DPI aware, so these are physical pixels).
struct MonitorInfo {
    int          number = 0;    // 1-based, ordered left-to-right then top-to-bottom
    std::wstring id;            // stable identifier (monitor device path; GDI name as fallback)
    std::wstring name;          // friendly name, e.g. "DELL U2720Q" or "Built-in display"
    std::wstring gdiName;       // e.g. \\.\DISPLAY1 (changes between sessions; not stored)
    RECT         rect{};        // monitor rectangle in virtual-screen coordinates
    bool         primary = false;

    // "1. DELL U2720Q - 3840 x 2160 (main)"
    std::wstring label() const;
};

// Make the process per-monitor DPI aware (so monitor rects are real pixels).
// Must run before any window is created. Safe on older Windows (falls back).
void enable_dpi_awareness();

// All active displays. Mirrored ("duplicate") displays appear once.
std::vector<MonitorInfo> enumerate_monitors();

// Which displays show the room. mode: 0 = main screen only, 1 = all screens,
// 2 = the screens whose id is in `chosenIds`. Chosen screens that aren't
// connected are skipped; if none of them are, the main screen is used, so the
// result is never empty while any display exists.
std::vector<MonitorInfo> pick_room_monitors(const std::vector<MonitorInfo>& all, int mode,
                                            const std::vector<std::wstring>& chosenIds);
