#include "core/log.h"
#include "core/version.h"

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <share.h>   // _SH_DENYWR for _wfsopen (shared-read logging)

static FILE* g_logFile = nullptr;

void log_init() {
    if (g_logFile) return;
    wchar_t tmp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || n > MAX_PATH) return;
    wchar_t path[MAX_PATH];
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%sRoomSaver.log", tmp);
    // Deny other writers but allow shared *reads* so the log can be tailed while
    // the app is still running.
    g_logFile = _wfsopen(path, L"w", _SH_DENYWR);
    log_msg("=== RoomSaver %s log start ===", ROOMSAVER_VERSION_STRING);
}

void log_msg(const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);

    OutputDebugStringA(buf);
    OutputDebugStringA("\n");

    if (g_logFile) {
        fputs(buf, g_logFile);
        fputc('\n', g_logFile);
        fflush(g_logFile);
    }
}
