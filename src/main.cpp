#include "core/platform.h"
#include "core/gfx_util.h"
#include "core/settings.h"
#include "core/config.h"
#include "core/monitors.h"
#include "core/log.h"
#include "app.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>

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

// ---------------------------------------------------------------------------
// Lock-on-exit (/s /lock). The PC must end up locked however the saver ends:
// normal dismissal, a startup failure, a crash or an unexpected error. Every
// one of those paths funnels into lock_now(), which locks at most once.
// ---------------------------------------------------------------------------
static std::atomic<bool> g_lockArmed{false};
static std::atomic<bool> g_lockDone{false};

// Debug builds only: ROOMSAVER_LOCK_DRYRUN=1 logs instead of locking, and
// ROOMSAVER_DEBUG_FAULT=initfail|crash|throw simulates a failure, so every
// lock path can be tested without locking the machine. Release builds have
// neither (a release /lock always locks).
#ifndef NDEBUG
static bool lock_dry_run() { return std::getenv("ROOMSAVER_LOCK_DRYRUN") != nullptr; }
static bool debug_fault(const char* kind) {
    const char* e = std::getenv("ROOMSAVER_DEBUG_FAULT");
    return e && std::strcmp(e, kind) == 0;
}
#else
static bool lock_dry_run() { return false; }
static bool debug_fault(const char*) { return false; }
#endif

static void lock_now(const char* why) {
    if (!g_lockArmed.load() || g_lockDone.exchange(true)) return;
    if (lock_dry_run()) { LOGI("LOCK_DRYRUN: would lock now (%s)", why); return; }
    // Lock first, log after: this also runs from crash handlers, where the log
    // file could be in a broken state.
    const BOOL ok = LockWorkStation();
    const DWORD err = ok ? 0 : GetLastError();
    // Our window is still up and covering the screen here, so Windows switches
    // to the lock screen over it with no flash of the unlocked desktop. The short
    // wait lets the secure desktop come up while the last frame is still shown.
    if (ok) Sleep(350);
    if (ok) LOGI("Locked the workstation (%s)", why);
    else    LOGI("LockWorkStation FAILED (error %lu) - the PC is NOT locked (%s)", err, why);
}

// Crash (access violation etc.) or an unhandled C++ exception, on any thread.
static LONG WINAPI on_unhandled_exception(EXCEPTION_POINTERS*) {
    lock_now("crash or unhandled error");
    return EXCEPTION_CONTINUE_SEARCH;   // let Windows Error Reporting carry on
}
// abort() on any thread, including the default std::terminate.
static void on_abort_signal(int) { lock_now("abort"); }
// An exception escaping the main thread.
static void on_terminate() {
    lock_now("unexpected error");
    std::_Exit(3);
}

static void arm_lock_on_exit() {
    g_lockArmed.store(true);
    SetUnhandledExceptionFilter(on_unhandled_exception);
    std::signal(SIGABRT, on_abort_signal);
    std::set_terminate(on_terminate);
}

// If the saver never became the active window it can't see key presses, which
// would go to whatever app is behind it instead. With /lock, give it a moment
// to get focus, then lock rather than keep covering a desktop that typing can
// still reach.
static constexpr double kFocusGraceSec = 2.0;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    log_init();
    // Real pixel coordinates on scaled / mixed-DPI setups (monitor rects, window size).
    enable_dpi_awareness();
    AppOptions opts = parse_command_line();

    if (opts.mode == Mode::Config)
        return run_config_dialog();

    const bool lockOnExit = opts.lockOnExit && opts.mode == Mode::FullScreen;
    if (lockOnExit) arm_lock_on_exit();

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
        lock_now("startup failed");
        platform.destroy();
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
    if (debug_fault("initfail") || !app.init(platform.width, platform.height)) {
        LOGI("App init failed");
        lock_now("startup failed");
        app.shutdown();
        platform.destroy();
        return 1;
    }

    LARGE_INTEGER freq, start, prev;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    prev = start;

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

        if (opts.mode == Mode::FullScreen) {
            const double running = double(now.QuadPart - start.QuadPart) / double(freq.QuadPart);
            static bool loggedFocus = false;
            if (platform.everActive && !loggedFocus) {
                LOGI("Screensaver has keyboard focus (after %.2f s)", running);
                loggedFocus = true;
            }
            // Backstop for the WM_ACTIVATE handling: another window is in front.
            if (platform.everActive && GetForegroundWindow() != platform.hwnd) {
                LOGI("Screensaver lost focus - ending");
                break;
            }
            if (lockOnExit && !platform.everActive && running > kFocusGraceSec) {
                LOGI("Screensaver never got keyboard focus - locking now");
                break;
            }
#ifndef NDEBUG
            if (running > 1.5 && debug_fault("crash")) *(volatile int*)nullptr = 0;
            if (running > 1.5 && debug_fault("throw")) throw std::runtime_error("debug fault");
#endif
        }

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

    // Secure screensaver: lock the PC BEFORE we tear down the window (see
    // lock_now). Locking at *startup* would instead hide the scene behind the
    // lock screen, so we lock the instant the saver is dismissed.
    lock_now("dismissed");

    app.shutdown();
    platform.destroy();
    return 0;
}
