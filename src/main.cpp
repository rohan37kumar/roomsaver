#include "core/platform.h"
#include "core/gfx_util.h"
#include "core/settings.h"
#include "core/config.h"
#include "core/log.h"
#include "app.h"

#include <windows.h>
#include <algorithm>
#include <cstdlib>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    log_init();
    AppOptions opts = parse_command_line();

    if (opts.mode == Mode::Config)
        return run_config_dialog();

    // Load user settings (registry) and hand them to the engine before init.
    Settings settings = Settings::load();

    Platform platform;
    if (!platform.create(opts)) {
        LOGI("Platform creation failed");
        return 1;
    }

    App app;
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
