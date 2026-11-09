#include "core/platform.h"
#include "core/gfx_util.h"
#include "core/settings.h"
#include "core/config.h"
#include "core/monitors.h"
#include "core/log.h"
#include "app.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

// Dev-only: ROOMSAVER_TEST_LAYOUT="x,y,w,h;x,y,w,h" puts the room into those
// rectangles of the window (client pixels, top-left origin) instead of filling
// it, so multi-screen layouts can be checked with --selftest on one monitor.
static std::vector<SceneRect> test_layout_from_env() {
    std::vector<SceneRect> out;
    const char* e = std::getenv("ROOMSAVER_TEST_LAYOUT");
    if (!e) return out;
    std::string s = e;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t end = s.find(';', pos);
        std::string part = s.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        SceneRect r;
        if (std::sscanf(part.c_str(), "%d,%d,%d,%d", &r.x, &r.y, &r.w, &r.h) == 4)
            out.push_back(r);
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return out;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    log_init();
    // Real pixel coordinates on scaled / mixed-DPI setups (monitor rects, window size).
    enable_dpi_awareness();
    AppOptions opts = parse_command_line();

    if (opts.mode == Mode::Config)
        return run_config_dialog();

    // Load user settings (registry) and hand them to the engine before init.
    Settings settings = Settings::load();

    // Fullscreen: pick which screens show the room (the rest go black).
    if (opts.mode == Mode::FullScreen) {
        const auto monitors = enumerate_monitors();
        const auto chosen = pick_room_monitors(monitors, (int)settings.displayMode,
                                               settings.displays);
        for (const MonitorInfo& m : monitors)
            LOGI("Screen %d: '%ls' %ldx%ld at (%ld,%ld)%s%s", m.number, m.name.c_str(),
                 m.rect.right - m.rect.left, m.rect.bottom - m.rect.top,
                 m.rect.left, m.rect.top, m.primary ? " main" : "",
                 std::any_of(chosen.begin(), chosen.end(),
                             [&](const MonitorInfo& c) { return c.id == m.id; }) ? " [room]" : "");
        for (const MonitorInfo& m : chosen) opts.roomScreens.push_back(m.rect);
    }

    Platform platform;
    if (!platform.create(opts)) {
        LOGI("Platform creation failed");
        return 1;
    }

    App app;
    for (const RECT& r : platform.roomRects)
        app.sceneRects.push_back({ (int)r.left, (int)r.top,
                                   (int)(r.right - r.left), (int)(r.bottom - r.top) });
    if (auto test = test_layout_from_env(); !test.empty())
        app.sceneRects = test;
    app.picturePathOverride = settings.picturePathUtf8();
    app.idleText            = settings.idleTextUtf8();
    app.beatSensitivity     = settings.beatSensitivity;
    app.brightness          = settings.brightness;
    if (!app.init(platform.width, platform.height)) {
        platform.destroy();
        return 1;
    }

    LARGE_INTEGER freq, prev;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    // Selftest normally captures frame 1. A delay lets async work (e.g. the SMTC
    // album-art fetch) land before the screenshot; frames keep rendering meanwhile.
    double selftestDelay = 0.0;
    if (const char* e = std::getenv("ROOMSAVER_SELFTEST_DELAY_MS"))
        selftestDelay = std::atof(e) / 1000.0;
    double selftestElapsed = 0.0;

    while (!platform.shouldQuit) {
        platform.pumpMessages();
        if (platform.shouldQuit) break;

        if (platform.resized) {
            platform.resized = false;
            app.resize(platform.width, platform.height);
        }

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double dt = double(now.QuadPart - prev.QuadPart) / double(freq.QuadPart);
        prev = now;
        dt = std::min(dt, 0.1);   // clamp so a hitch/resume doesn't jump animation

        app.update(dt);
        app.render();

        if (opts.selftest) {
            selftestElapsed += dt;
            if (selftestElapsed >= selftestDelay) {
                // Read the freshly rendered back buffer before swapping.
                save_screenshot(opts.selftestOut.c_str(), platform.width, platform.height);
                break;
            }
            platform.swap();   // keep rendering until the delay elapses
            continue;
        }

        platform.swap();
    }

    // Secure screensaver: lock the PC BEFORE we tear down the window. Our
    // fullscreen topmost window is still covering the screen here, so Windows
    // switches to the lock screen over it — no split-second flash of the unlocked
    // desktop. (Locking at *startup* would instead hide the scene behind the lock
    // screen, so we lock the instant the saver is dismissed.) The short wait lets
    // the secure desktop come up while the last frame is still on screen.
    if (opts.lockOnExit && opts.mode == Mode::FullScreen) {
        LockWorkStation();
        Sleep(350);
    }

    app.shutdown();
    platform.destroy();
    return 0;
}
