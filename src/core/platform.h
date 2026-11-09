#pragma once

#include <windows.h>
#include <string>
#include <vector>

// Screensaver run modes, derived from the command line Windows passes to a .scr.
enum class Mode {
    Windowed,   // dev/testing: normal resizable window, closes normally
    FullScreen, // /s : real screensaver, exits on any input
    Preview,    // /p <HWND> : renders inside the settings-dialog preview window
    Config      // /c : configuration dialog
};

struct AppOptions {
    Mode mode = Mode::Windowed;
    HWND previewParent = nullptr;   // valid in Preview mode
    bool selftest = false;          // render one frame, screenshot, exit
    std::wstring selftestOut;       // screenshot path for selftest
    bool lockOnExit = false;        // /lock : lock the workstation when the saver ends
    int width  = 1280;
    int height = 720;
    // FullScreen only: monitor rects (virtual-screen coords) that show the room.
    // The window covers every monitor; areas outside these rects stay black.
    std::vector<RECT> roomScreens;
};

// Owns the Win32 window and the WGL/OpenGL context.
struct Platform {
    HWND  hwnd  = nullptr;
    HDC   hdc   = nullptr;
    HGLRC hglrc = nullptr;
    int   width  = 0;
    int   height = 0;
    Mode  mode = Mode::Windowed;
    bool  shouldQuit = false;

    // True once the window has been the active (keyboard-focus) window. In
    // FullScreen mode, losing activation after that ends the saver, like any
    // other input: otherwise keystrokes would go to a hidden app behind it.
    bool  everActive = false;

    // Set to true when a client rect change was detected (consume + reset).
    bool  resized = false;

    // FullScreen: opts.roomScreens translated to window client coordinates
    // (top-left origin). Empty in other modes = the room fills the window.
    std::vector<RECT> roomRects;

    bool create(const AppOptions& opts);
    void pumpMessages();   // processes the message queue; may set shouldQuit
    void swap();           // SwapBuffers
    void destroy();
};

// Parse the raw command line into options (handles /s /p /c /w /lock, --selftest).
AppOptions parse_command_line();
