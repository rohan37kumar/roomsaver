#include "core/settings.h"
#include "core/log.h"
#include "core/utf8.h"

#include <windows.h>
#include <cmath>
#include <string>

namespace {

const wchar_t* kKey = L"Software\\RoomSaver";

// Floats are stored as DWORDs scaled x1000 so the registry stays human-readable.
DWORD f2d(float v) { return (DWORD)std::lround(v * 1000.0f); }
float d2f(DWORD v) { return (float)v / 1000.0f; }

bool read_dword(HKEY key, const wchar_t* name, DWORD& out) {
    DWORD type = 0, size = sizeof(DWORD);
    return RegQueryValueExW(key, name, nullptr, &type, (LPBYTE)&out, &size) == ERROR_SUCCESS
           && type == REG_DWORD;
}

// Returns true if the value exists (even if empty), so an intentionally blank
// string can be told apart from "never set".
bool read_string(HKEY key, const wchar_t* name, std::wstring& out) {
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        type != REG_SZ)
        return false;
    out.assign(size / sizeof(wchar_t), L'\0');
    if (size > 0 &&
        RegQueryValueExW(key, name, nullptr, nullptr, (LPBYTE)out.data(), &size) != ERROR_SUCCESS)
        return false;
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return true;
}

void write_string(HKEY key, const wchar_t* name, const std::wstring& v) {
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)v.c_str(),
                   (DWORD)((v.size() + 1) * sizeof(wchar_t)));
}

} // namespace

Settings Settings::load() {
    Settings s;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        read_string(key, L"PicturePath", s.picturePath);
        read_string(key, L"IdleText", s.idleText);     // absent -> keeps default "kc"
        DWORD d = 0;
        if (read_dword(key, L"BeatSensitivity", d)) s.beatSensitivity = d2f(d);
        if (read_dword(key, L"Brightness", d))      s.brightness      = d2f(d);
        RegCloseKey(key);
    }
    // Clamp to sane ranges regardless of what was on disk.
    if (s.beatSensitivity < 0.2f) s.beatSensitivity = 0.2f;
    if (s.beatSensitivity > 3.0f) s.beatSensitivity = 3.0f;
    if (s.brightness < 0.5f) s.brightness = 0.5f;
    if (s.brightness > 2.0f) s.brightness = 2.0f;
    if (s.idleText.size() > kIdleTextMax) s.idleText.resize(kIdleTextMax);
    return s;
}

void Settings::save() const {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr)
        != ERROR_SUCCESS) {
        LOGI("settings: RegCreateKeyEx failed");
        return;
    }
    write_string(key, L"PicturePath", picturePath);
    write_string(key, L"IdleText", idleText);
    DWORD sens = f2d(beatSensitivity), bri = f2d(brightness);
    RegSetValueExW(key, L"BeatSensitivity", 0, REG_DWORD, (const BYTE*)&sens, sizeof(sens));
    RegSetValueExW(key, L"Brightness",      0, REG_DWORD, (const BYTE*)&bri,  sizeof(bri));
    RegCloseKey(key);
}

std::string Settings::picturePathUtf8() const { return wide_to_utf8(picturePath); }
std::string Settings::idleTextUtf8() const    { return wide_to_utf8(idleText); }

// --- process helpers ---------------------------------------------------------

std::wstring exe_path() {
    // Grow the buffer until the full path fits (paths can exceed MAX_PATH).
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (n == 0) return {};
        if (n < buf.size()) { buf.resize(n); return buf; }
        buf.resize(buf.size() * 2);
    }
}

std::string exe_dir_utf8() {
    std::wstring p = exe_path();
    size_t s = p.find_last_of(L"/\\");
    return wide_to_utf8(s == std::wstring::npos ? p : p.substr(0, s));
}
