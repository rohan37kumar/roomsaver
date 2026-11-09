#include "core/config.h"
#include "core/settings.h"
#include "core/monitors.h"
#include "core/resource.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

// Trackbars work in integer units; store settings as value*100 so 0.01 steps.
namespace {
constexpr int SENS_MIN = 20,  SENS_MAX = 300;   // 0.20 .. 3.00
constexpr int BRI_MIN  = 50,  BRI_MAX  = 200;   // 0.50 .. 2.00

// One row of the screen checklist: a connected screen, or a previously chosen
// screen that isn't connected right now (kept so re-docking restores it).
struct ScreenRow {
    std::wstring id;
    std::wstring name;
    bool connected = false;
};
std::vector<ScreenRow>   g_rows;       // rows of IDC_SCREENLIST, in order
std::vector<MonitorInfo> g_monitors;   // connected screens (for Identify)

void set_val_label(HWND dlg, int labelId, int hundredths) {
    wchar_t buf[16];
    swprintf_s(buf, L"%.2f", hundredths / 100.0f);
    SetDlgItemTextW(dlg, labelId, buf);
}

void browse_for_picture(HWND dlg) {
    wchar_t path[MAX_PATH] = L"";
    GetDlgItemTextW(dlg, IDC_PICPATH, path, MAX_PATH);

    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner   = dlg;
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp\0All Files\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn))
        SetDlgItemTextW(dlg, IDC_PICPATH, path);
}

// ---- Screens ---------------------------------------------------------------

void fill_screen_list(HWND dlg, const Settings& s) {
    HWND list = GetDlgItem(dlg, IDC_SCREENLIST);
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    RECT rc{};
    GetClientRect(list, &rc);
    LVCOLUMNW col{};
    col.mask = LVCF_WIDTH;
    col.cx   = rc.right - rc.left - GetSystemMetrics(SM_CXVSCROLL);
    ListView_InsertColumn(list, 0, &col);

    g_monitors = enumerate_monitors();
    g_rows.clear();
    std::vector<std::wstring> labels;
    for (const MonitorInfo& m : g_monitors) {
        g_rows.push_back({ m.id, m.name, true });
        labels.push_back(m.label());
    }
    for (size_t i = 0; i < s.displays.size(); ++i) {
        const std::wstring& id = s.displays[i];
        bool present = std::any_of(g_rows.begin(), g_rows.end(),
                                   [&](const ScreenRow& r) { return r.id == id; });
        if (present) continue;
        std::wstring name = i < s.displayNames.size() ? s.displayNames[i] : L"Display";
        g_rows.push_back({ id, name, false });
        labels.push_back(name + L" (not connected)");
    }

    for (size_t i = 0; i < g_rows.size(); ++i) {
        LVITEMW it{};
        it.mask     = LVIF_TEXT;
        it.iItem    = (int)i;
        it.pszText  = labels[i].data();
        ListView_InsertItem(list, &it);
        bool chosen = std::find(s.displays.begin(), s.displays.end(), g_rows[i].id)
                      != s.displays.end();
        ListView_SetCheckState(list, (int)i, chosen);
    }
}

void update_screen_controls(HWND dlg) {
    int mode = (int)SendDlgItemMessageW(dlg, IDC_SCREENMODE, CB_GETCURSEL, 0, 0);
    EnableWindow(GetDlgItem(dlg, IDC_SCREENLIST), mode == (int)DisplayMode::Chosen);
}

// "Identify": flash each connected screen's number (matching the list) for a
// couple of seconds, like Windows' display settings.
constexpr UINT_PTR kIdentifyTimer = 1;
const wchar_t*     kIdentifyClass = L"RoomSaverIdentify";

LRESULT CALLBACK identify_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT rc;
        GetClientRect(w, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(20, 22, 28));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        HFONT font = CreateFontW(-(rc.bottom - rc.top) * 2 / 3, 0, 0, 0, FW_BOLD, FALSE, FALSE,
                                 FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(240, 240, 240));
        wchar_t num[8];
        swprintf_s(num, L"%d", (int)GetWindowLongPtrW(w, GWLP_USERDATA));
        DrawTextW(dc, num, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, old);
        DeleteObject(font);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_TIMER:
    case WM_LBUTTONDOWN:
        DestroyWindow(w);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

void identify_screens(HWND dlg) {
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc   = identify_proc;
        wc.hInstance     = inst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kIdentifyClass;
        registered = RegisterClassW(&wc) != 0;
    }
    g_monitors = enumerate_monitors();   // refresh in case screens changed
    for (const MonitorInfo& m : g_monitors) {
        int mw = m.rect.right - m.rect.left, mh = m.rect.bottom - m.rect.top;
        int size = std::max(120, std::min(mw, mh) / 4);
        HWND w = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                 kIdentifyClass, L"", WS_POPUP,
                                 m.rect.left + (mw - size) / 2, m.rect.top + (mh - size) / 2,
                                 size, size, dlg, nullptr, inst, nullptr);
        if (!w) continue;
        SetWindowLongPtrW(w, GWLP_USERDATA, m.number);
        ShowWindow(w, SW_SHOWNOACTIVATE);
        SetTimer(w, kIdentifyTimer, 2500, nullptr);
    }
}

// ---- Dialog ----------------------------------------------------------------

INT_PTR CALLBACK dlg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        Settings s = Settings::load();

        HINSTANCE inst = GetModuleHandleW(nullptr);
        auto icon = [&](int size) {
            return (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, size, size, 0);
        };
        SendMessageW(dlg, WM_SETICON, ICON_BIG,   (LPARAM)icon(GetSystemMetrics(SM_CXICON)));
        SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)icon(GetSystemMetrics(SM_CXSMICON)));

        SetDlgItemTextW(dlg, IDC_PICPATH, s.picturePath.c_str());
        SendDlgItemMessageW(dlg, IDC_IDLETEXT, EM_LIMITTEXT, Settings::kIdleTextMax, 0);
        SetDlgItemTextW(dlg, IDC_IDLETEXT, s.idleText.c_str());

        HWND modeBox = GetDlgItem(dlg, IDC_SCREENMODE);
        SendMessageW(modeBox, CB_ADDSTRING, 0, (LPARAM)L"Main screen only");
        SendMessageW(modeBox, CB_ADDSTRING, 0, (LPARAM)L"All screens");
        SendMessageW(modeBox, CB_ADDSTRING, 0, (LPARAM)L"Chosen screens (tick below)");
        SendMessageW(modeBox, CB_SETCURSEL, (WPARAM)s.displayMode, 0);
        fill_screen_list(dlg, s);
        update_screen_controls(dlg);

        HWND sens = GetDlgItem(dlg, IDC_SENS);
        SendMessageW(sens, TBM_SETRANGE, TRUE, MAKELONG(SENS_MIN, SENS_MAX));
        int sv = (int)lround(s.beatSensitivity * 100.0f);
        SendMessageW(sens, TBM_SETPOS, TRUE, sv);
        set_val_label(dlg, IDC_SENS_VAL, sv);

        HWND bri = GetDlgItem(dlg, IDC_BRIGHT);
        SendMessageW(bri, TBM_SETRANGE, TRUE, MAKELONG(BRI_MIN, BRI_MAX));
        int bv = (int)lround(s.brightness * 100.0f);
        SendMessageW(bri, TBM_SETPOS, TRUE, bv);
        set_val_label(dlg, IDC_BRIGHT_VAL, bv);

        return TRUE;
    }

    case WM_HSCROLL: {
        HWND ctl = (HWND)lp;
        if (ctl == GetDlgItem(dlg, IDC_SENS))
            set_val_label(dlg, IDC_SENS_VAL, (int)SendMessageW(ctl, TBM_GETPOS, 0, 0));
        else if (ctl == GetDlgItem(dlg, IDC_BRIGHT))
            set_val_label(dlg, IDC_BRIGHT_VAL, (int)SendMessageW(ctl, TBM_GETPOS, 0, 0));
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BROWSE:
            browse_for_picture(dlg);
            return TRUE;

        case IDC_IDENTIFY:
            identify_screens(dlg);
            return TRUE;

        case IDC_SCREENMODE:
            if (HIWORD(wp) == CBN_SELCHANGE) update_screen_controls(dlg);
            return TRUE;

        case IDOK: {
            Settings s;
            wchar_t path[MAX_PATH] = L"";
            GetDlgItemTextW(dlg, IDC_PICPATH, path, MAX_PATH);
            s.picturePath     = path;
            wchar_t idle[Settings::kIdleTextMax + 1] = L"";
            GetDlgItemTextW(dlg, IDC_IDLETEXT, idle, (int)std::size(idle));
            s.idleText        = idle;
            s.beatSensitivity = (int)SendMessageW(GetDlgItem(dlg, IDC_SENS),   TBM_GETPOS, 0, 0) / 100.0f;
            s.brightness      = (int)SendMessageW(GetDlgItem(dlg, IDC_BRIGHT), TBM_GETPOS, 0, 0) / 100.0f;

            int mode = (int)SendDlgItemMessageW(dlg, IDC_SCREENMODE, CB_GETCURSEL, 0, 0);
            if (mode < 0 || mode > (int)DisplayMode::Chosen) mode = 0;
            s.displayMode = (DisplayMode)mode;
            HWND list = GetDlgItem(dlg, IDC_SCREENLIST);
            for (size_t i = 0; i < g_rows.size(); ++i)
                if (ListView_GetCheckState(list, (int)i)) {
                    s.displays.push_back(g_rows[i].id);
                    s.displayNames.push_back(g_rows[i].name);
                }
            s.save();

            EndDialog(dlg, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

} // namespace

int run_config_dialog() {
    // Trackbars (msctls_trackbar32) + the screen checklist (SysListView32).
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_CONFIG),
                    nullptr, dlg_proc, 0);
    return 0;
}
