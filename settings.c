/* settings.c — 设置界面（内嵌于单 exe 中，通过 --settings 参数启动） */
#include "winmover.h"

/* ─── 控件 ID ─────────────────────────────────────── */
#define IDC_STARTUP          1001
#define IDC_MANUAL_OVERRIDE  1002
#define IDC_MANUAL_ACTIVE    1003
#define IDC_TASKBAR_MOVE     1004
#define IDC_FULLSCREEN       1005
#define IDC_PRIMARY_SCALE    1006
#define IDC_SECONDARY_SCALE  1007
#define IDC_BRIGHTNESS_SLIDER 1008
#define IDC_BRIGHTNESS_TEXT  1009
#define IDC_ORIENTATION      1010
#define IDC_SIDE_LEFT        1011
#define IDC_SIDE_RIGHT       1012
#define IDC_SAVE_BTN         1013
#define IDC_RESTORE_BTN      1014
#define IDC_STATUS_TEXT      1015
#define IDC_SWITCHER         1016

/* ─── 控件全局句柄 ─────────────────────────────────── */
static HWND g_hStatus, g_hStartup, g_hManualOvr, g_hManualAct,
            g_hTaskbar, g_hFullscreen, g_hSwitcher,
            g_hPriScale, g_hSecScale,
            g_hBrightSlider, g_hBrightText,
            g_hOrientation, g_hSideL, g_hSideR,
            g_hSave, g_hRestore;
static HFONT  g_hFont, g_hFontSmall, g_hFontBold;
static HBRUSH g_hBgBrush, g_hEditBgBrush;
static Config g_oldCfg;  /* 用于检测变更 */

/* ─── 深色主题 ─────────────────────────────────────── */
static void InitTheme(HWND hWnd) {
    g_hBgBrush    = CreateSolidBrush(RGB(0x20, 0x20, 0x20));
    g_hEditBgBrush = CreateSolidBrush(RGB(0x2B, 0x2B, 0x2B));
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

/* ─── 更新控件值 ───────────────────────────────────── */
static void UpdateControls(void) {
    char buf[64];
    int monCnt = GetMonitorCount();
    int active = g_cfg.ManualOverride ? g_cfg.ManualActive : (monCnt >= 2);

    _snprintf(buf, sizeof(buf), "\xd7\xb4\xcc\xac: %s (%d \xb8\xf6\xcf\xd4\xca\xbe\xc6\xf7)",
              active ? "\xbf\xaa" : "\xb9\xd8", monCnt);
    SetWindowTextA(g_hStatus, buf);

    SendMessage(g_hManualOvr, BM_SETCHECK, g_cfg.ManualOverride ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_hManualAct, BM_SETCHECK, g_cfg.ManualActive ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(g_hManualAct, g_cfg.ManualOverride ? TRUE : FALSE);
    SendMessage(g_hTaskbar,    BM_SETCHECK, g_cfg.TaskbarMoveEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_hFullscreen, BM_SETCHECK, g_cfg.FullScreenMode ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_hStartup,    BM_SETCHECK, g_cfg.StartupEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_hSwitcher,   BM_SETCHECK, g_cfg.SwitcherEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

    _snprintf(buf, sizeof(buf), "%d", g_cfg.PrimaryScale);
    SetWindowTextA(g_hPriScale, buf);
    _snprintf(buf, sizeof(buf), "%d", g_cfg.SecondaryScale);
    SetWindowTextA(g_hSecScale, buf);

    SendMessage(g_hOrientation, CB_SETCURSEL, (WPARAM)g_cfg.SecondaryOrientation, 0);
    SendMessage(g_hSideL, BM_SETCHECK, g_cfg.SecondarySide == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_hSideR, BM_SETCHECK, g_cfg.SecondarySide == 1 ? BST_CHECKED : BST_UNCHECKED, 0);

    int bright = g_cfg.SavedBrightness;
    if (bright < 0) {
        int actual = GetSecondaryBrightness();
        if (actual >= 0) { bright = actual; g_cfg.SavedBrightness = actual; SaveConfig(); }
    }
    SendMessage(g_hBrightSlider, TBM_SETPOS, TRUE, bright >= 0 ? bright : 0);
    EnableWindow(g_hBrightSlider, bright >= 0 ? TRUE : FALSE);
    _snprintf(buf, sizeof(buf), bright >= 0 ? "%d%%" : "\xb2\xbb\xd6\xa7\xb3\xd6", bright >= 0 ? bright : 0);
    SetWindowTextA(g_hBrightText, buf);
}

/* ─── 从界面保存配置 ───────────────────────────────── */
static void SaveFromUI(void) {
    char buf[32];

    g_cfg.ManualOverride     = (SendMessage(g_hManualOvr, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    g_cfg.ManualActive       = (SendMessage(g_hManualAct, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    g_cfg.TaskbarMoveEnabled = (SendMessage(g_hTaskbar, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    g_cfg.FullScreenMode     = (SendMessage(g_hFullscreen, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    g_cfg.StartupEnabled     = (SendMessage(g_hStartup, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    g_cfg.SwitcherEnabled    = (SendMessage(g_hSwitcher, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;

    GetWindowTextA(g_hPriScale, buf, sizeof(buf));
    int v = atoi(buf);
    if (v < 50 || v > 500) v = 150;
    g_cfg.PrimaryScale = v;
    _snprintf(buf, sizeof(buf), "%d", v);
    SetWindowTextA(g_hPriScale, buf);

    GetWindowTextA(g_hSecScale, buf, sizeof(buf));
    v = atoi(buf);
    if (v < 50 || v > 500) v = 125;
    g_cfg.SecondaryScale = v;
    _snprintf(buf, sizeof(buf), "%d", v);
    SetWindowTextA(g_hSecScale, buf);

    int sel = (int)SendMessage(g_hOrientation, CB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel <= 3) g_cfg.SecondaryOrientation = sel;

    g_cfg.SecondarySide = (SendMessage(g_hSideL, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 0 : 1;

    if (IsWindowEnabled(g_hBrightSlider)) {
        g_cfg.SavedBrightness = (int)SendMessage(g_hBrightSlider, TBM_GETPOS, 0, 0);
        SetSecondaryBrightness(g_cfg.SavedBrightness);
    }

    SaveConfig();
    g_oldCfg = g_cfg;
    UpdateControls();
}

/* ─── 窗口过程 ─────────────────────────────────────── */
static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(0x20, 0x20, 0x20));
        SetTextColor(hdc, RGB(0xE0, 0xE0, 0xE0));
        return (LRESULT)g_hBgBrush;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(0x2B, 0x2B, 0x2B));
        SetTextColor(hdc, RGB(0xFF, 0xFF, 0xFF));
        return (LRESULT)g_hEditBgBrush;
    }
        SetBkColor((HDC)wParam, RGB(0x20, 0x20, 0x20));
        SetTextColor((HDC)wParam, RGB(0xE0, 0xE0, 0xE0));
        return (LRESULT)g_hBgBrush;
    case WM_COMMAND: {
        int id = LOWORD(wParam), code = HIWORD(wParam);
        if (id == IDC_MANUAL_OVERRIDE && code == BN_CLICKED) {
            int chk = (SendMessage(g_hManualOvr, BM_GETCHECK, 0, 0) == BST_CHECKED);
            EnableWindow(g_hManualAct, chk ? TRUE : FALSE);
        }
        else if ((id == IDC_SIDE_LEFT || id == IDC_SIDE_RIGHT) && code == BN_CLICKED) {
            g_cfg.SecondarySide = (id == IDC_SIDE_LEFT) ? 0 : 1;
            if (GetMonitorCount() >= 2)
                SetOrientationAndSide(g_cfg.SecondaryOrientation, g_cfg.SecondarySide);
            SaveConfig();
            UpdateControls();
        }
        else if (id == IDC_ORIENTATION && code == CBN_SELCHANGE) {
            int sel = (int)SendMessage(g_hOrientation, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel <= 3) {
                g_cfg.SecondaryOrientation = sel;
                if (GetMonitorCount() >= 2)
                    SetOrientationAndSide(sel, g_cfg.SecondarySide);
                SaveConfig();
                UpdateControls();
            }
        }
        else if (id == IDC_SAVE_BTN && code == BN_CLICKED) {
            SaveFromUI();
            SetWindowTextA(g_hStatus, "\xd2\xd1\xb1\xa3\xb4\xe6");
            SetTimer(hWnd, 2, 2000, NULL);
        }
        else if (id == IDC_RESTORE_BTN && code == BN_CLICKED) {
            g_cfg.PrimaryScale         = 150;
            g_cfg.SecondaryScale       = 125;
            g_cfg.SecondaryOrientation = 0;
            g_cfg.SecondarySide        = 0;
            g_cfg.FullScreenMode       = 0;
            g_cfg.TaskbarMoveEnabled   = 1;
            g_cfg.ManualOverride       = 0;
            g_cfg.ManualActive         = 1;
            g_cfg.StartupEnabled       = 1;
            g_cfg.SwitcherEnabled      = 1;
            /* 亮度不恢复 */
            SaveConfig();
            UpdateControls();
            if (GetMonitorCount() >= 2)
                SetOrientationAndSide(g_cfg.SecondaryOrientation, g_cfg.SecondarySide);
            SetWindowTextA(g_hStatus, "\xd2\xd1\xbb\xd6\xb8\xb4\xc4\xac\xc8\xcf");
            SetTimer(hWnd, 2, 2000, NULL);
        }
        break;
    }
    case WM_HSCROLL:
        if ((HWND)lParam == g_hBrightSlider) {
            int v = (int)SendMessage(g_hBrightSlider, TBM_GETPOS, 0, 0);
            char buf[16]; _snprintf(buf, sizeof(buf), "%d%%", v);
            SetWindowTextA(g_hBrightText, buf);
            if (LOWORD(wParam) == TB_THUMBPOSITION || LOWORD(wParam) == TB_ENDTRACK) {
                g_cfg.SavedBrightness = v;
                SetSecondaryBrightness(v);
                SaveConfig();
            }
        }
        break;
    case WM_TIMER:
        if (wParam == 2) { KillTimer(hWnd, 2); UpdateControls(); }
        return 0;
    case WM_CLOSE:
        g_guiHwnd = NULL;
        g_guiOpenFlag = 0;
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

/* ─── 创建控件 ─────────────────────────────────────── */
static HWND CreateDarkBtn(HWND parent, int x, int y, int w, int h, const char *t, int id) {
    HWND btn = CreateWindowA("BUTTON", t, WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON|BS_CENTER,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
    SendMessage(btn, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return btn;
}

static void CreateControls(HWND hWnd) {
    int xm = 20, ww = 680, row = 0;
    const char *oriItems[] = {
        "\xba\xe1\xcf\xf2(0)", "\xd7\xdd\xcf\xf2(90)",
        "\xba\xe1\xb7\xad(180)", "\xd7\xdd\xb7\xad(270)"
    };

    g_hStatus = CreateWindowA("STATIC", "\xb5\xc8\xb4\xfd...",
                              WS_VISIBLE|WS_CHILD, xm, 15, ww, 22, hWnd,
                              (HMENU)(INT_PTR)IDC_STATUS_TEXT, NULL, NULL);
    SendMessage(g_hStatus, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

    row = 42;
    CreateWindowA("BUTTON", "\xb9\xa6\xc4\xdc\xbf\xd8\xd6\xc6",
                  WS_VISIBLE|WS_CHILD|BS_GROUPBOX,
                  xm, row, ww, 140, hWnd, NULL, NULL, NULL);
    SendMessage(GetWindow(hWnd, GW_CHILD), WM_SETFONT, (WPARAM)g_hFont, TRUE);

    g_hStartup = CreateWindowA("BUTTON", "\xbf\xaa\xbb\xfa\xd7\xd4\xc6\xf4\xb6\xaf",
                   WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                   35, row+22, 130, 20, hWnd, (HMENU)(INT_PTR)IDC_STARTUP, NULL, NULL);
    SendMessage(g_hStartup, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    g_hManualOvr = CreateWindowA("BUTTON", "\xca\xd6\xb6\xaf\xb8\xb2\xb8\xc7\xd7\xd4\xb6\xaf\xbc\xec\xb2\xe2",
                    WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                    230, row+22, 170, 20, hWnd, (HMENU)(INT_PTR)IDC_MANUAL_OVERRIDE, NULL, NULL);
    SendMessage(g_hManualOvr, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    g_hManualAct = CreateWindowA("BUTTON", "\xc6\xf4\xd3\xc3\xb4\xb0\xbf\xda\xd2\xc6\xb6\xaf",
                   WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                    420, row+22, 160, 20, hWnd, (HMENU)(INT_PTR)IDC_MANUAL_ACTIVE, NULL, NULL);
    SendMessage(g_hManualAct, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    g_hTaskbar = CreateWindowA("BUTTON", "\xc8\xce\xce\xf1\xc0\xb8\xbf\xe7\xc6\xc1\xd2\xc6\xb6\xaf",
                  WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                  35, row+48, 150, 20, hWnd, (HMENU)(INT_PTR)IDC_TASKBAR_MOVE, NULL, NULL);
    SendMessage(g_hTaskbar, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    g_hFullscreen = CreateWindowA("BUTTON", "\xc8\xab\xc6\xc1\xc4\xa3\xca\xbd\xa3\xa8\xb8\xb1\xc6\xc1\xd7\xee\xb4\xf3\xbb\xaf\xa3\xa9",
                     WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                     35, row+74, 280, 20, hWnd, (HMENU)(INT_PTR)IDC_FULLSCREEN, NULL, NULL);
    SendMessage(g_hFullscreen, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    g_hSwitcher = CreateWindowA("BUTTON", "\xb4\xb0\xbf\xda\xc7\xd0\xbb\xbb\xc6\xf7",
                   WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                   35, row+96, 150, 20, hWnd, (HMENU)(INT_PTR)IDC_SWITCHER, NULL, NULL);
    SendMessage(g_hSwitcher, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    row += 147;
    CreateWindowA("BUTTON", "DPI \xcb\xf5\xb7\xc5\xb1\xc8\xc0\xfd(%)",
                  WS_VISIBLE|WS_CHILD|BS_GROUPBOX,
                  xm, row, ww, 100, hWnd, NULL, NULL, NULL);
    SendMessage(GetWindow(hWnd, GW_CHILD), WM_SETFONT, (WPARAM)g_hFont, TRUE);

    CreateWindowA("STATIC", "\xd6\xf7\xc6\xc1:", WS_VISIBLE|WS_CHILD, 35, row+24, 55, 18,
                  hWnd, NULL, NULL, NULL);
    SendMessage(GetWindow(hWnd, GW_CHILD), WM_SETFONT, (WPARAM)g_hFont, TRUE);

    g_hPriScale = CreateWindowA("EDIT", "150", WS_VISIBLE|WS_CHILD|WS_BORDER|ES_NUMBER,
                                95, row+21, 60, 22, hWnd, (HMENU)(INT_PTR)IDC_PRIMARY_SCALE, NULL, NULL);
    SendMessage(g_hPriScale, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    CreateWindowA("STATIC", "\xb8\xb1\xc6\xc1:", WS_VISIBLE|WS_CHILD, 260, row+24, 55, 18,
                  hWnd, NULL, NULL, NULL);
    SendMessage(GetWindow(hWnd, GW_CHILD), WM_SETFONT, (WPARAM)g_hFont, TRUE);

    g_hSecScale = CreateWindowA("EDIT", "125", WS_VISIBLE|WS_CHILD|WS_BORDER|ES_NUMBER,
                                320, row+21, 60, 22, hWnd, (HMENU)(INT_PTR)IDC_SECONDARY_SCALE, NULL, NULL);
    SendMessage(g_hSecScale, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    CreateWindowA("STATIC", "\xd2\xc6\xb5\xbd\xb8\xb1\xc6\xc1\xca\xb1\xd7\xd4\xb6\xaf\xcb\xf5\xb7\xc5",
                  WS_VISIBLE|WS_CHILD, 35, row+50, 350, 22, hWnd, NULL, NULL, NULL);
    SendMessage(GetWindow(hWnd, GW_CHILD), WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

    row += 110;
    CreateWindowA("BUTTON", "\xc1\xc1\xb6\xc8\xb5\xf7\xbd\xda (DDC/CI)",
                  WS_VISIBLE|WS_CHILD|BS_GROUPBOX,
                  xm, row, ww, 72, hWnd, NULL, NULL, NULL);
    SendMessage(GetWindow(hWnd, GW_CHILD), WM_SETFONT, (WPARAM)g_hFont, TRUE);

    g_hBrightSlider = CreateWindowA(TRACKBAR_CLASSA, "",
                        WS_VISIBLE|WS_CHILD|TBS_AUTOTICKS|TBS_ENABLESELRANGE,
                        35, row+24, 500, 28, hWnd, (HMENU)(INT_PTR)IDC_BRIGHTNESS_SLIDER, NULL, NULL);
    SendMessage(g_hBrightSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessage(g_hBrightSlider, TBM_SETTICFREQ, 10, 0);

    g_hBrightText = CreateWindowA("STATIC", "0%", WS_VISIBLE|WS_CHILD,
                                  550, row+26, 50, 18, hWnd,
                                  (HMENU)(INT_PTR)IDC_BRIGHTNESS_TEXT, NULL, NULL);
    SendMessage(g_hBrightText, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    row += 92;
    CreateWindowA("BUTTON", "\xb8\xb1\xc6\xc1\xb7\xbd\xcf\xf2\xd3\xeb\xce\xbb\xd6\xc3",
                  WS_VISIBLE|WS_CHILD|BS_GROUPBOX, xm, row, ww, 120, hWnd, NULL, NULL, NULL);
    SendMessage(GetWindow(hWnd, GW_CHILD), WM_SETFONT, (WPARAM)g_hFont, TRUE);

    CreateWindowA("STATIC", "\xb7\xbd\xcf\xf2:", WS_VISIBLE|WS_CHILD, 35, row+24, 55, 20,
                  hWnd, NULL, NULL, NULL);
    SendMessage(GetWindow(hWnd, GW_CHILD), WM_SETFONT, (WPARAM)g_hFont, TRUE);

    g_hOrientation = CreateWindowA("COMBOBOX", "",
                      WS_VISIBLE|WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL,
                      165, row+21, 230, 120, hWnd, (HMENU)(INT_PTR)IDC_ORIENTATION, NULL, NULL);
    SendMessage(g_hOrientation, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    { int i; for (i = 0; i < 4; i++)
        SendMessage(g_hOrientation, CB_ADDSTRING, 0, (LPARAM)oriItems[i]); }

    CreateWindowA("STATIC", "\xce\xbb\xd6\xc3:", WS_VISIBLE|WS_CHILD, 35, row+52, 55, 20,
                  hWnd, NULL, NULL, NULL);
    SendMessage(GetWindow(hWnd, GW_CHILD), WM_SETFONT, (WPARAM)g_hFont, TRUE);

    g_hSideL = CreateWindowA("BUTTON", "\xd7\xf3\xb2\xe0",
                             WS_VISIBLE|WS_CHILD|BS_AUTORADIOBUTTON,
                             130, row+50, 60, 20, hWnd, (HMENU)(INT_PTR)IDC_SIDE_LEFT, NULL, NULL);
    SendMessage(g_hSideL, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    g_hSideR = CreateWindowA("BUTTON", "\xd3\xd2\xb2\xe0",
                             WS_VISIBLE|WS_CHILD|BS_AUTORADIOBUTTON,
                             200, row+50, 70, 20, hWnd, (HMENU)(INT_PTR)IDC_SIDE_RIGHT, NULL, NULL);
    SendMessage(g_hSideR, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    CreateWindowA("STATIC", "\xb8\xc4\xce\xbb\xd6\xc3\xba\xf3\xd7\xd4\xb6\xaf\xd3\xa6\xd3\xc3\xb7\xbd\xcf\xf2",
                  WS_VISIBLE|WS_CHILD, 35, row+76, 350, 22, hWnd, NULL, NULL, NULL);
    SendMessage(GetWindow(hWnd, GW_CHILD), WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

    row += 135;
    CreateWindowA("BUTTON", "\xbf\xec\xbd\xdd\xbc\xfc",
                  WS_VISIBLE|WS_CHILD|BS_GROUPBOX,
                  xm, row, ww, 96, hWnd, NULL, NULL, NULL);
    SendMessage(GetWindow(hWnd, GW_CHILD), WM_SETFONT, (WPARAM)g_hFont, TRUE);

    CreateWindowA("STATIC", "Win + Alt + P    \xc7\xd0\xbb\xbb\xcf\xd4\xca\xbe\xc4\xa3\xca\xbd(\xc4\xda/\xcd\xe2)",
                  WS_VISIBLE|WS_CHILD, 35, row+22, 500, 18, hWnd, NULL, NULL, NULL);
    SendMessage(GetWindow(hWnd, GW_CHILD), WM_SETFONT, (WPARAM)g_hFont, TRUE);

    CreateWindowA("STATIC", "Win + Alt + O    \xd1\xad\xbb\xb7\xc7\xd0\xbb\xbb\xb8\xb1\xc6\xc1\xb7\xbd\xcf\xf2",
                  WS_VISIBLE|WS_CHILD, 35, row+44, 400, 18, hWnd, NULL, NULL, NULL);
    SendMessage(GetWindow(hWnd, GW_CHILD), WM_SETFONT, (WPARAM)g_hFont, TRUE);

    CreateWindowA("STATIC", "Win + Shift + Tab    \xb4\xb0\xbf\xda\xc7\xd0\xbb\xbb\xc6\xf7",
                  WS_VISIBLE|WS_CHILD, 35, row+66, 400, 18, hWnd, NULL, NULL, NULL);
    SendMessage(GetWindow(hWnd, GW_CHILD), WM_SETFONT, (WPARAM)g_hFont, TRUE);

    row += 106;
    g_hSave = CreateDarkBtn(hWnd, 35, row, 90, 30, "\xb1\xa3\xb4\xe6", IDC_SAVE_BTN);
    g_hRestore = CreateDarkBtn(hWnd, 140, row, 110, 30, "\xbb\xd6\xb8\xb4\xc4\xac\xc8\xcf", IDC_RESTORE_BTN);

    LoadConfig();
    g_oldCfg = g_cfg;
    UpdateControls();
}

/* ─── 设置界面入口 ─────────────────────────────────── */
int RunSettingsGui(HINSTANCE hInst, int nCmdShow) {
    /* 单实例 */
    HANDLE hMutex = CreateMutexA(NULL, FALSE, "WindowMoveSettingsMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hW = FindWindowA("WindowMoveSettingsClass", NULL);
        if (hW) { ShowWindow(hW, SW_SHOW); SetForegroundWindow(hW); }
        CloseHandle(hMutex);
        return 0;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = SettingsWndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIconA(hInst, MAKEINTRESOURCEA(1));
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0x20, 0x20, 0x20));
    wc.lpszClassName = "WindowMoveSettingsClass";
    RegisterClassA(&wc);

    /* 字体：系统默认 + 加大 */
    {
        LOGFONTA lf;
        ZeroMemory(&lf, sizeof(lf));
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = DEFAULT_QUALITY;
        lf.lfHeight = -16;
        g_hFont = CreateFontIndirectA(&lf);
        lf.lfHeight = -13;
        g_hFontSmall = CreateFontIndirectA(&lf);
        lf.lfHeight = -17;
        lf.lfWeight = FW_SEMIBOLD;
        g_hFontBold = CreateFontIndirectA(&lf);
    }

    int winW = 720, winH = 700;
    RECT wr = {0, 0, winW, winH};
    AdjustWindowRect(&wr, WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX, FALSE);
    int adjW = wr.right - wr.left, adjH = wr.bottom - wr.top;
    int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);

    HWND hWnd = CreateWindowExA(0, "WindowMoveSettingsClass",
                                "Secondary Display Assistant",
                                WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
                                (scrW-adjW)/2, (scrH-adjH)/2, adjW, adjH,
                                NULL, NULL, hInst, NULL);
    if (!hWnd) { CloseHandle(hMutex); return 1; }
    InitTheme(hWnd);
    g_guiHwnd = hWnd;

    CreateControls(hWnd);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_hBgBrush)    DeleteObject(g_hBgBrush);
    if (g_hEditBgBrush) DeleteObject(g_hEditBgBrush);
    if (g_hFont)      DeleteObject(g_hFont);
    if (g_hFontSmall) DeleteObject(g_hFontSmall);
    if (g_hFontBold)  DeleteObject(g_hFontBold);
    CloseHandle(hMutex);
    return 0;
}
