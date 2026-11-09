#pragma once

// UTF-8 <-> UTF-16 helpers. The engine keeps paths as UTF-8 std::string; these
// open them through the wide Win32 CRT so install folders with non-ASCII
// characters (e.g. C:\Users\José\...) work. Plain fopen/ifstream would read the
// bytes as the ANSI code page instead.

#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

inline std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

inline std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

inline FILE* fopen_utf8(const std::string& path, const wchar_t* mode) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, utf8_to_wide(path).c_str(), mode) != 0) return nullptr;
    return f;
}

// Read a whole file (binary). Returns false if it can't be opened.
inline bool read_file_utf8(const std::string& path, std::vector<unsigned char>& out) {
    FILE* f = fopen_utf8(path, L"rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len < 0) { std::fclose(f); return false; }
    out.resize((size_t)len);
    size_t got = len > 0 ? std::fread(out.data(), 1, (size_t)len, f) : 0;
    std::fclose(f);
    out.resize(got);
    return true;
}
