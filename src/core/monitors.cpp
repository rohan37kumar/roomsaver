#include "core/monitors.h"

#include <algorithm>
#include <cwchar>
#include <map>

namespace {

struct TargetInfo {
    std::wstring id;
    std::wstring name;
    bool internal = false;
};

// Map each GDI source (\\.\DISPLAYn) to its monitor's stable device path and
// friendly name via the DisplayConfig API. Mirrored outputs share one source;
// the first target wins.
std::map<std::wstring, TargetInfo> query_targets() {
    std::map<std::wstring, TargetInfo> out;
    UINT32 nPaths = 0, nModes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPaths, &nModes) != ERROR_SUCCESS)
        return out;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(nPaths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nModes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPaths, paths.data(), &nModes, modes.data(),
                           nullptr) != ERROR_SUCCESS)
        return out;
    paths.resize(nPaths);

    for (const DISPLAYCONFIG_PATH_INFO& p : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
        src.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size      = sizeof(src);
        src.header.adapterId = p.sourceInfo.adapterId;
        src.header.id        = p.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS) continue;

        DISPLAYCONFIG_TARGET_DEVICE_NAME tgt{};
        tgt.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tgt.header.size      = sizeof(tgt);
        tgt.header.adapterId = p.targetInfo.adapterId;
        tgt.header.id        = p.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tgt.header) != ERROR_SUCCESS) continue;

        std::wstring key = src.viewGdiDeviceName;
        if (out.count(key)) continue;

        TargetInfo t;
        t.id   = tgt.monitorDevicePath;
        t.name = tgt.monitorFriendlyDeviceName;
        const auto tech = tgt.outputTechnology;
        t.internal = tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
                     tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
                     tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
        out[key] = t;
    }
    return out;
}

BOOL CALLBACK enum_proc(HMONITOR hm, HDC, LPRECT, LPARAM lp) {
    auto* list = reinterpret_cast<std::vector<MonitorInfo>*>(lp);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hm, &mi)) {
        MonitorInfo m;
        m.gdiName = mi.szDevice;
        m.rect    = mi.rcMonitor;
        m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        list->push_back(m);
    }
    return TRUE;
}

} // namespace

std::wstring MonitorInfo::label() const {
    wchar_t buf[256];
    swprintf_s(buf, L"%d. %s - %d x %d%s", number, name.c_str(),
               (int)(rect.right - rect.left), (int)(rect.bottom - rect.top),
               primary ? L" (main)" : L"");
    return buf;
}

void enable_dpi_awareness() {
    using SetCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (auto setCtx = (SetCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext")) {
        if (setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
    SetProcessDPIAware();   // pre-1703 fallback: system DPI aware
}

std::vector<MonitorInfo> enumerate_monitors() {
    std::vector<MonitorInfo> list;
    EnumDisplayMonitors(nullptr, nullptr, enum_proc, reinterpret_cast<LPARAM>(&list));

    std::sort(list.begin(), list.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        if (a.rect.left != b.rect.left) return a.rect.left < b.rect.left;
        return a.rect.top < b.rect.top;
    });

    const auto targets = query_targets();
    for (size_t i = 0; i < list.size(); ++i) {
        MonitorInfo& m = list[i];
        m.number = (int)i + 1;
        auto it = targets.find(m.gdiName);
        if (it != targets.end() && !it->second.id.empty()) {
            m.id   = it->second.id;
            m.name = !it->second.name.empty() ? it->second.name
                   : it->second.internal      ? L"Built-in display"
                                              : L"Display";
        } else {
            m.id   = m.gdiName;
            m.name = L"Display";
        }
    }
    return list;
}

std::vector<MonitorInfo> pick_room_monitors(const std::vector<MonitorInfo>& all, int mode,
                                            const std::vector<std::wstring>& chosenIds) {
    std::vector<MonitorInfo> out;
    if (mode == 1) {
        out = all;
    } else if (mode == 2) {
        for (const MonitorInfo& m : all)
            if (std::find(chosenIds.begin(), chosenIds.end(), m.id) != chosenIds.end())
                out.push_back(m);
    }
    if (out.empty()) {   // main screen only, or none of the chosen screens connected
        for (const MonitorInfo& m : all)
            if (m.primary) { out.push_back(m); break; }
        if (out.empty() && !all.empty()) out.push_back(all.front());
    }
    return out;
}
