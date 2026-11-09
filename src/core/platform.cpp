#include "core/platform.h"
#include "core/log.h"
#include "core/resource.h"

#include <glad/gl.h>
#include <shellapi.h>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
// WGL extension constants / typedefs (avoid depending on <GL/wglext.h>)
// ---------------------------------------------------------------------------
#define WGL_DRAW_TO_WINDOW_ARB        0x2001
#define WGL_ACCELERATION_ARB          0x2003
#define WGL_SUPPORT_OPENGL_ARB        0x2010
#define WGL_DOUBLE_BUFFER_ARB         0x2011
#define WGL_PIXEL_TYPE_ARB            0x2013
#define WGL_COLOR_BITS_ARB            0x2014
#define WGL_DEPTH_BITS_ARB            0x2022
#define WGL_STENCIL_BITS_ARB          0x2023
#define WGL_FULL_ACCELERATION_ARB     0x2027
#define WGL_TYPE_RGBA_ARB             0x202B
#define WGL_SAMPLE_BUFFERS_ARB        0x2041
#define WGL_SAMPLES_ARB               0x2042

#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_FLAGS_ARB         0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB  0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x0002

typedef BOOL  (WINAPI* PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
typedef HGLRC (WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
typedef BOOL  (WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int);

static PFNWGLCHOOSEPIXELFORMATARBPROC    wglChoosePixelFormatARB    = nullptr;
static PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;
static PFNWGLSWAPINTERVALEXTPROC         wglSwapIntervalEXT         = nullptr;

static const wchar_t* kWndClass = L"RoomSaverWindowClass";
static HMODULE        g_opengl32 = nullptr;

// GL function loader for glad, with the opengl32.dll fallback for 1.1 symbols.
static void* gl_get_proc(const char* name) {
    void* p = (void*)wglGetProcAddress(name);
    if (p == nullptr || p == (void*)0x1 || p == (void*)0x2 ||
        p == (void*)0x3 || p == (void*)-1) {
        if (!g_opengl32) g_opengl32 = LoadLibraryA("opengl32.dll");
        p = (void*)GetProcAddress(g_opengl32, name);
    }
    return p;
}

// ---------------------------------------------------------------------------
// One-time bootstrap: a throwaway context to fetch the ARB entry points.
// ---------------------------------------------------------------------------
static bool bootstrap_wgl() {
    if (wglCreateContextAttribsARB) return true;

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"RoomSaverDummyWGL";
    RegisterClassW(&wc);

    HWND dummy = CreateWindowExW(0, wc.lpszClassName, L"dummy", 0,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
                                 nullptr, nullptr, wc.hInstance, nullptr);
    HDC ddc = GetDC(dummy);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(ddc, &pfd);
    SetPixelFormat(ddc, pf, &pfd);

    HGLRC drc = wglCreateContext(ddc);
    wglMakeCurrent(ddc, drc);

    wglChoosePixelFormatARB    = (PFNWGLCHOOSEPIXELFORMATARBPROC)   wglGetProcAddress("wglChoosePixelFormatARB");
    wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
    wglSwapIntervalEXT         = (PFNWGLSWAPINTERVALEXTPROC)        wglGetProcAddress("wglSwapIntervalEXT");

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(drc);
    ReleaseDC(dummy, ddc);
    DestroyWindow(dummy);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    if (!wglCreateContextAttribsARB || !wglChoosePixelFormatARB) {
        LOGI("bootstrap_wgl: failed to load ARB entry points");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Platform* self = (Platform*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    const bool saver = (self->mode == Mode::FullScreen);

    switch (msg) {
    case WM_SIZE:
        self->width  = LOWORD(lp);
        self->height = HIWORD(lp);
        self->resized = true;
        return 0;

    case WM_CLOSE:
    case WM_DESTROY:
        self->shouldQuit = true; if (!self->quitReason) self->quitReason = "window closed";
        PostQuitMessage(0);
        return 0;

    case WM_SETCURSOR:
        if (saver) { SetCursor(nullptr); return TRUE; }
        break;

    // --- focus: another window taking over dismisses the screensaver ---
    // If some other app became active while we keep covering the screen, typed
    // keys would reach it unseen (and never dismiss us), so treat it as input.
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) self->everActive = true;
        else if (saver && self->everActive) { self->shouldQuit = true; self->quitReason = "another window was activated"; PostQuitMessage(0); }
        break;
    case WM_ACTIVATEAPP:
        if (wp) self->everActive = true;
        else if (saver && self->everActive) { self->shouldQuit = true; self->quitReason = "another app was activated"; PostQuitMessage(0); }
        break;

    // --- input dismisses the screensaver in fullscreen mode ---
    case WM_MOUSEMOVE: {
        if (saver) {
            static bool  havePrev = false;
            static POINT prev{};
            POINT pt{ (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
            if (!havePrev) { prev = pt; havePrev = true; }
            else if (abs(pt.x - prev.x) > 6 || abs(pt.y - prev.y) > 6) {
                self->shouldQuit = true; self->quitReason = "mouse moved"; PostQuitMessage(0);
            }
        }
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (saver) { self->shouldQuit = true; self->quitReason = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) ? "key pressed" : "mouse button/wheel"; PostQuitMessage(0); }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Context creation on a real window
// ---------------------------------------------------------------------------
static bool create_context(Platform& p) {
    const int pfAttribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
        WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
        WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB,     32,
        WGL_DEPTH_BITS_ARB,     24,
        WGL_STENCIL_BITS_ARB,   8,
        // No MSAA on the window itself: the scene is antialiased in PostFX's
        // offscreen MSAA target, and the fullscreen window can span several
        // monitors (a multisampled back buffer that size would waste memory).
        0
    };
    int   pixelFormat = 0;
    UINT  numFormats  = 0;
    wglChoosePixelFormatARB(p.hdc, pfAttribs, nullptr, 1, &pixelFormat, &numFormats);
    if (!numFormats) { LOGI("wglChoosePixelFormatARB found no format"); return false; }

    PIXELFORMATDESCRIPTOR pfd = {};
    DescribePixelFormat(p.hdc, pixelFormat, sizeof(pfd), &pfd);
    if (!SetPixelFormat(p.hdc, pixelFormat, &pfd)) { LOGI("SetPixelFormat failed"); return false; }

    const int ctxAttribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };
    p.hglrc = wglCreateContextAttribsARB(p.hdc, nullptr, ctxAttribs);
    if (!p.hglrc) { LOGI("wglCreateContextAttribsARB failed"); return false; }

    wglMakeCurrent(p.hdc, p.hglrc);
    if (!gladLoadGL((GLADloadfunc)gl_get_proc)) { LOGI("gladLoadGL failed"); return false; }
    if (wglSwapIntervalEXT) wglSwapIntervalEXT(1);   // vsync

    LOGI("GL_VENDOR   : %s", (const char*)glGetString(GL_VENDOR));
    LOGI("GL_RENDERER : %s", (const char*)glGetString(GL_RENDERER));
    LOGI("GL_VERSION  : %s", (const char*)glGetString(GL_VERSION));
    return true;
}

bool Platform::create(const AppOptions& opts) {
    mode = opts.mode;

    if (!bootstrap_wgl()) return false;

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.lpszClassName = kWndClass;
    RegisterClassW(&wc);

    DWORD style = 0, exStyle = 0;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = opts.width, h = opts.height;
    HWND parent = nullptr;

    switch (mode) {
    case Mode::FullScreen: {
        // One borderless window over the whole virtual desktop (every monitor).
        // The room renders into roomRects; everything else stays black.
        x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (w <= 0 || h <= 0) {   // paranoia: fall back to the primary monitor
            x = 0; y = 0;
            w = GetSystemMetrics(SM_CXSCREEN);
            h = GetSystemMetrics(SM_CYSCREEN);
        }
        for (const RECT& r : opts.roomScreens)
            roomRects.push_back({ r.left - x, r.top - y, r.right - x, r.bottom - y });
        style   = WS_POPUP | WS_VISIBLE;
        exStyle = WS_EX_TOPMOST;
        LOGI("FullScreen: window %dx%d at (%d,%d), room on %zu screen(s)",
             w, h, x, y, roomRects.size());
        break;
    }
    case Mode::Preview: {
        parent = opts.previewParent;
        if (!parent || !IsWindow(parent)) {
            LOGI("Preview mode: missing or invalid parent window handle");
            return false;
        }
        RECT rc{};
        GetClientRect(parent, &rc);
        x = 0; y = 0; w = rc.right - rc.left; h = rc.bottom - rc.top;
        style = WS_CHILD | WS_VISIBLE;
        break;
    }
    case Mode::Windowed:
    default: {
        style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        RECT rc{ 0, 0, w, h };
        AdjustWindowRect(&rc, style, FALSE);
        w = rc.right - rc.left; h = rc.bottom - rc.top;
        break;
    }
    }

    hwnd = CreateWindowExW(exStyle, kWndClass, L"RoomSaver", style,
                           x, y, w, h, parent, nullptr, inst, this);
    if (!hwnd) { LOGI("CreateWindowExW failed"); return false; }

    hdc = GetDC(hwnd);
    if (!create_context(*this)) return false;

    // width/height get set by the WM_SIZE that ShowWindow triggers; seed them too.
    RECT rc{};
    GetClientRect(hwnd, &rc);
    width  = rc.right - rc.left;
    height = rc.bottom - rc.top;

    if (mode == Mode::FullScreen) {
        ShowCursor(FALSE);
        SetForegroundWindow(hwnd);
    }

    return true;
}

void Platform::pumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { shouldQuit = true; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void Platform::swap() {
    SwapBuffers(hdc);
}

void Platform::destroy() {
    if (mode == Mode::FullScreen) ShowCursor(TRUE);
    if (hglrc) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(hglrc); hglrc = nullptr; }
    if (hdc && hwnd) { ReleaseDC(hwnd, hdc); hdc = nullptr; }
    if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; }
}

// ---------------------------------------------------------------------------
// Command-line parsing
//   /s              full-screen screensaver
//   /p <hwnd>       preview (hwnd may be next arg or after a ':')
//   /c  /c:<hwnd>   configure
//   /w              windowed dev mode (default when no args)
//   --selftest[=path]   render one frame, screenshot to path, exit
// ---------------------------------------------------------------------------
static HWND parse_hwnd(const wchar_t* s) {
    return (HWND)(uintptr_t)_wcstoui64(s, nullptr, 10);
}

AppOptions parse_command_line() {
    AppOptions o;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    for (int i = 1; i < argc; ++i) {
        const std::wstring raw = argv[i];     // original case (paths)
        std::wstring a = raw;                 // lowercased copy (flag matching)
        for (auto& ch : a) ch = (wchar_t)towlower(ch);
        if (a.empty()) continue;

        if (a.rfind(L"--selftest", 0) == 0) {
            o.selftest = true;
            o.mode = Mode::Windowed;
            o.width = 1280; o.height = 720;
            size_t eq = raw.find(L'=');
            o.selftestOut = (eq != std::wstring::npos) ? raw.substr(eq + 1)
                                                       : L"selftest.png";
        } else if (a == L"/lock" || a == L"-lock" || a == L"--lock") {
            o.lockOnExit = true;        // pairs with /s: lock the PC on dismissal
        } else if (a == L"/lockwatch") {
            // Internal: started by a /s /lock saver as its hidden watchdog.
            if (i + 1 < argc) o.lockWatchHandle = _wcstoui64(argv[++i], nullptr, 10);
            if (!o.lockWatchHandle) o.lockWatchHandle = ~0ull;   // malformed: still a watchdog
        } else if (a[0] == L'/' || a[0] == L'-') {
            wchar_t f = a.size() > 1 ? a[1] : 0;
            if (f == L's') {
                o.mode = Mode::FullScreen;
            } else if (f == L'c') {
                o.mode = Mode::Config;
            } else if (f == L'w') {
                o.mode = Mode::Windowed;
            } else if (f == L'p') {
                o.mode = Mode::Preview;
                size_t colon = a.find(L':');
                if (colon != std::wstring::npos && colon + 1 < a.size())
                    o.previewParent = parse_hwnd(a.c_str() + colon + 1);
                else if (i + 1 < argc)
                    o.previewParent = parse_hwnd(argv[++i]);
            }
        }
    }
    LocalFree(argv);
    return o;
}
