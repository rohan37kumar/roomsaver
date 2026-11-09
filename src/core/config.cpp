#include "core/config.h"
#include "core/settings.h"
#include "core/resource.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <cmath>
#include <iterator>
#include <string>

// Trackbars work in integer units; store settings as value*100 so 0.01 steps.
namespace {
constexpr int SENS_MIN = 20,  SENS_MAX = 300;   // 0.20 .. 3.00
constexpr int BRI_MIN  = 50,  BRI_MAX  = 200;   // 0.50 .. 2.00

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

INT_PTR CALLBACK dlg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        Settings s = Settings::load();

        SetDlgItemTextW(dlg, IDC_PICPATH, s.picturePath.c_str());
        SendDlgItemMessageW(dlg, IDC_IDLETEXT, EM_LIMITTEXT, Settings::kIdleTextMax, 0);
        SetDlgItemTextW(dlg, IDC_IDLETEXT, s.idleText.c_str());

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
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);   // register the trackbar (msctls_trackbar32) class

    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_CONFIG),
                    nullptr, dlg_proc, 0);
    return 0;
}
