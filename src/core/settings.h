#pragma once

#include <string>
#include <vector>

// Which screens show the room in fullscreen (/s) mode; all others go black.
enum class DisplayMode : int {
    Main   = 0,   // main (primary) screen only
    All    = 1,   // every connected screen
    Chosen = 2    // the screens listed in Settings::displays
};

// User-facing settings, persisted in the registry under HKCU\Software\RoomSaver.
// Shared by every run mode (screensaver /s, preview /p, config /c) so the look
// stays identical wherever the scene is hosted.
struct Settings {
    static constexpr size_t kIdleTextMax = 24;   // chars accepted for the idle text

    std::wstring picturePath;              // framed picture; empty -> assets/picture.*
    std::wstring idleText = L"kc";         // TV wordmark when nothing is playing ("" = blank)
    float        beatSensitivity = 1.0f;   // audio onset scale  (0.20 .. 3.00)
    float        brightness      = 1.0f;   // global scene brightness (0.50 .. 2.00)

    DisplayMode  displayMode = DisplayMode::Main;
    std::vector<std::wstring> displays;      // chosen screen ids (MonitorInfo::id)
    std::vector<std::wstring> displayNames;  // their friendly names (shown when disconnected)

    static Settings load();            // read from the registry (defaults if absent)
    void           save() const;       // write to the registry

    // UTF-8 copies (what the engine's std::string loaders expect).
    std::string picturePathUtf8() const;
    std::string idleTextUtf8() const;
};

// --- process helpers ---------------------------------------------------------
std::wstring exe_path();               // full path of this executable
std::string  exe_dir_utf8();           // directory containing the exe (UTF-8)
