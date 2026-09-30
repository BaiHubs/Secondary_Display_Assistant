/* winmover.c — 主程序：Shell Hook + 低层键盘钩子 + 热键 + 单进程 */
#include "winmover.h"

Config  g_cfg;
char    g_exeDir[MAX_PATH];
char    g_iniPath[MAX_PATH];
int     g_isActive        = 0;
int     g_movingFlag      = 0;
int     g_resizingFlag    = 0;
int     g_taskbarMoveEnabled = 1;
int     g_fullScreenMode  = 0;
int     g_primaryScale    = 150;
int     g_secondaryScale  = 125;
HWND    g_guiHwnd         = NULL;
int     g_guiOpenFlag     = 0;
HINSTANCE g_hInst;
HWND    g_hMainWnd;

static UINT      g_uShellHookMsg = 0;
static int       g_switcherActive = 0;
static POINT     g_swHookCursor = {0, 0};
static void AltTabShow(void);

static void GetExeDir(void) {
    GetModuleFileNameA(NULL, g_exeDir, MAX_PATH);
    char *p = strrchr(g_exeDir, '\\');
    if (p) *p = '\0';
}

void UpdateActiveState(void) {
    g_isActive = g_cfg.ManualOverride
                 ? g_cfg.ManualActive
                 : (GetMonitorCount() >= 2);
}

/* ─── 设置界面入口（单进程） ─────────────────────────── */
void OpenSettingsGui(void) {
    if (g_guiOpenFlag) {
        if (g_guiHwnd && IsWindow(g_guiHwnd)) {
            SetForegroundWindow(g_guiHwnd);
            return;
        }
        g_guiOpenFlag = 0;
    }
    /* 运行内建的设置界面 */
    RunSettingsGui(g_hInst, SW_SHOW);
    g_guiOpenFlag = 1;
}

/* ─── 热键（用 RegisterHotKey 替代钩子，零系统开销） ─ */
#define IDH_DISPLAY   9001
#define IDH_SWITCHER  9002
#define IDH_ORIENT    9003

/* ─── Shell Hook ──────────────────────────────────── */
#define HSHELL_WINDOWCREATED   1
#define HSHELL_WINDOWACTIVATED 4

static int IsRealAppWindow(HWND hWnd) {
    if (!IsWindow(hWnd)) return 0;
    if (IsExcludedWindow(hWnd)) return 0;

    /* 必须有可见标题栏 */
    LONG style = GetWindowLongA(hWnd, GWL_STYLE);
    if (!(style & WS_CAPTION)) return 0;
    if (!(style & WS_VISIBLE)) return 0;

    /* 必须有非空标题 */
    char title[128];
    if (!GetWindowTextA(hWnd, title, sizeof(title))) return 0;
    if (title[0] == '\0') return 0;

    /* 检查窗口类名 — 排除桌面和任务栏，保留文件资源管理器 */
    char cls[64];
    if (GetClassNameA(hWnd, cls, sizeof(cls))) {
        if (strcmp(cls, "Progman") == 0 ||         /* 桌面 */
            strcmp(cls, "WorkerW") == 0 ||           /* 桌面背景 */
            strcmp(cls, "Shell_TrayWnd") == 0 ||     /* 任务栏 */
            strcmp(cls, "Shell_SecondaryTrayWnd") == 0)
            return 0;
    }

    /* 排除系统进程（保留 explorer.exe — 文件资源管理器可移动） */
    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (!pid) return 0;
    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hp) {
        char path[MAX_PATH]; DWORD sz = MAX_PATH;
        if (QueryFullProcessImageNameA(hp, 0, path, &sz)) {
            char *fn = strrchr(path, '\\');
            fn = fn ? fn + 1 : path;
            if (_stricmp(fn, "svchost.exe")  == 0 ||
                _stricmp(fn, "rundll32.exe") == 0 ||
                _stricmp(fn, "shellexperiencehost.exe") == 0) {
                CloseHandle(hp);
                return 0;
            }
        }
        CloseHandle(hp);
    }
    return 1;
}

/* ─── 防抖：全局节流 + 前台窗口校验 ────────────────── */
static DWORD g_lastActivationTick = 0;
static DWORD g_lastDestroyTick    = 0;

/* ─── 鼠标是否在任务栏附近（含预览弹窗区域） ──────── */

static int IsCursorNearTaskbar(void) {
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
    if (!hMon) return 0;
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfo(hMon, &mi)) return 0;
    const int MARGIN = 250; /* 预览弹窗可能出现在任务栏上方 250px 内 */
    /* 底部任务栏 */
    if (mi.rcWork.bottom < mi.rcMonitor.bottom &&
        pt.y >= mi.rcWork.bottom - MARGIN && pt.y <= mi.rcMonitor.bottom + 1)
        return 1;
    /* 顶部任务栏 */
    if (mi.rcWork.top > mi.rcMonitor.top &&
        pt.y >= mi.rcMonitor.top - 1 && pt.y <= mi.rcWork.top + MARGIN)
        return 1;
    /* 左侧任务栏 */
    if (mi.rcWork.left > mi.rcMonitor.left &&
        pt.x >= mi.rcMonitor.left - 1 && pt.x <= mi.rcWork.left + MARGIN)
        return 1;
    /* 右侧任务栏 */
    if (mi.rcWork.right < mi.rcMonitor.right &&
        pt.x >= mi.rcWork.right - MARGIN && pt.x <= mi.rcMonitor.right + 1)
        return 1;
    return 0;
}

static void OnWindowCreated(HWND hWnd) {
    if (!g_isActive) return;
    if (!IsWindow(hWnd)) return;
    if (IsExcludedWindow(hWnd)) return;
    LONG s = GetWindowLongA(hWnd, GWL_STYLE);
    if (!(s & WS_CAPTION)) return;
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR hMonMouse = GetMonitorFromPointEx(pt.x, pt.y);
    DelayedMove(hWnd, hMonMouse);
}

/* ─── HSHELL_WINDOWACTIVATED ──────────────────────── */
/* 简单可靠：用 shell hook 的 hWnd + GetForegroundWindow 回退验证 */
static void OnWindowActivated(HWND hWnd) {
    if (!g_isActive || !g_taskbarMoveEnabled || g_movingFlag) return;
    if (GetTickCount() - g_lastActivationTick < 300) return;
    if (GetTickCount() - g_lastDestroyTick < 200) return;

    /* 验证：shell hook 的窗口必须是当前前台，否则用前台窗口替代 */
    {
        HWND fgWnd = GetForegroundWindow();
        if (hWnd != fgWnd) hWnd = fgWnd;
    }
    if (!hWnd || !IsRealAppWindow(hWnd)) return;
    if (!IsCursorNearTaskbar()) return;

    POINT pt;
    GetCursorPos(&pt);
    HMONITOR hMonMouse = GetMonitorFromPointEx(pt.x, pt.y);
    HMONITOR hMonWnd   = GetMonitorFromWindowEx(hWnd);
    if (hMonWnd == hMonMouse) return;

    g_lastActivationTick = GetTickCount();

    if (IsIconic(hWnd)) {
        ShowWindow(hWnd, SW_RESTORE);
        Sleep(80);
    }

    g_movingFlag = 1;
    MoveWindowToMonitor(hWnd, hMonMouse, g_fullScreenMode);
    g_movingFlag = 0;
}

/* ─── 窗口切换器（Alt+Tab / Ctrl+Alt+Tab 增强叠加） ─── */
typedef struct { HWND hWnd; char title[256]; } WinEntry;

/* 辅助：将窗口移到光标所在屏幕 */
static void MoveWinToCursorMon(HWND hWnd) {
    POINT pt; GetCursorPos(&pt);
    HMONITOR hMonMouse = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
    HMONITOR hMonWnd = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONULL);
    if (hMonMouse && hMonWnd && hMonMouse != hMonWnd) {
        RECT rc; GetMonitorRect(hMonMouse, &rc);
        int cx = rc.left + (rc.right - rc.left - 800) / 2;
        int cy = rc.top + (rc.bottom - rc.top - 600) / 2;
        SetWindowPos(hWnd, NULL, cx > 0 ? cx : 0, cy > 0 ? cy : 0,
                     800, 600, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void ShowMonitorSwitcher(void) {
    static int busy = 0;
    if (busy) return;
    busy = 1;
    WinEntry prim[64], sec[64];
    int primCnt = 0, secCnt = 0, total = 0;

    /* 枚举窗口，按主屏/副屏分组 */
    HWND hWnd = GetTopWindow(NULL);
    while (hWnd && total < 128) {
        if (IsWindowVisible(hWnd) && IsRealAppWindow(hWnd)) {
            char title[256];
            if (GetWindowTextA(hWnd, title, sizeof(title)) && title[0]) {
                HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONULL);
                if (hMon && IsPrimaryMonitor(hMon) && primCnt < 64)
                    { prim[primCnt].hWnd = hWnd; strcpy(prim[primCnt].title, title); primCnt++; total++; }
                else if (hMon && !IsPrimaryMonitor(hMon) && secCnt < 64)
                    { sec[secCnt].hWnd = hWnd; strcpy(sec[secCnt].title, title); secCnt++; total++; }
            }
        }
        hWnd = GetNextWindow(hWnd, GW_HWNDNEXT);
    }
    if (total == 0) return;

    /* 构建显示文本 */
    char txt[4096] = "";
    strcat(txt, "=== \xd6\xf7\xc6\xc1 \xb4\xb0\xbf\xda ===\r\n");
    for (int i = 0; i < primCnt; i++) {
        char line[300]; _snprintf(line, sizeof(line), " [%d] %s\r\n", i+1, prim[i].title);
        strcat(txt, line);
    }
    strcat(txt, "\r\n=== \xb8\xb1\xc6\xc1 \xb4\xb0\xbf\xda ===\r\n");
    for (int i = 0; i < secCnt; i++) {
        char line[300]; _snprintf(line, sizeof(line), " [%d] %s\r\n", primCnt+i+1, sec[i].title);
        strcat(txt, line);
    }

    /* 在光标所在屏幕创建叠加窗口 */
    POINT pt; GetCursorPos(&pt);
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
    RECT rcMon = {0}; GetMonitorRect(hMon, &rcMon);
    int winW = 500, winH = 400;
    int x = rcMon.left + (rcMon.right - rcMon.left - winW) / 2;
    int y = rcMon.top + (rcMon.bottom - rcMon.top - winH) / 2;

    HWND hOverlay = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "STATIC", "Alt+Tab \xd4\xf6\xc7\xbf \xb4\xb0\xbf\xda\xc7\xd0\xbb\xbb",
        WS_VISIBLE | WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, winW, winH, NULL, NULL, g_hInst, NULL);
    if (!hOverlay) { MessageBoxA(0, txt, "Window Switcher", MB_OK); return; }

    HWND hText = CreateWindowExA(0, "EDIT", txt,
        WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
        10, 10, winW-25, winH-60, hOverlay, NULL, g_hInst, NULL);
    SendMessageA(hText, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SetForegroundWindow(hOverlay);

    /* 消息循环（PeekMessage 不阻塞主窗口消息处理） */
    MSG msg;
    int done = 0;
    while (!done && IsWindow(hOverlay)) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_KEYDOWN) {
                int vk = (int)msg.wParam;
                if (vk >= '1' && vk <= '9') {
                    int idx = vk - '0';
                    HWND target = NULL;
                    if (idx >= 1 && idx <= primCnt) target = prim[idx-1].hWnd;
                    else if (idx > primCnt && idx <= total) target = sec[idx-primCnt-1].hWnd;
                    if (target) { MoveWinToCursorMon(target); SetForegroundWindow(target); }
                    done = 1; break;
                }
                if (vk == VK_ESCAPE) { done = 1; break; }
            }
            if (msg.message == WM_DESTROY) { done = 1; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!done) Sleep(20);
    }
    if (IsWindow(hOverlay)) DestroyWindow(hOverlay);
    busy = 0;
}

/* ─── Alt+Tab 仿原生切换器（DWM 缩略图） ──────────── */
#define THUMB_W 300
#define THUMB_H 200
#define TITLE_H 36
#define MARGIN  30
#define GAP     20
#define GAP_ROW 80
#define SEP_W   2

typedef struct { HWND hwnd; HTHUMBNAIL thumb; char title[256];
                 int isPrimary; RECT rcDst; } SwItem;
static SwItem g_swItems[64];
static int g_swCount = 0, g_swSel = 0;
static HWND g_swHwnd = NULL;
static HMONITOR g_swTargetMon = NULL;

void AltTabConfirm(void);
void AltTabCancel(void);

static LRESULT CALLBACK SwitcherWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);

        /* 背景 */
        HBRUSH bg = CreateSolidBrush(RGB(0x1E, 0x1E, 0x1E));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0xFF, 0xFF, 0xFF));

        /* 绘制选中框 */
        if (g_swSel >= 0 && g_swSel < g_swCount) {
            RECT *sr = &g_swItems[g_swSel].rcDst;
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(0x00, 0x78, 0xD7));
            HGDIOBJ old = SelectObject(hdc, pen);
            HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, sr->left-3, sr->top-3, sr->right+3, sr->bottom+3);
            SelectObject(hdc, old); SelectObject(hdc, oldBr);
            DeleteObject(pen);
        }

        /* 窗口标题 */
        HFONT font = CreateFontA(-16, 0,0,0, FW_NORMAL,0,0,0, DEFAULT_CHARSET,0,0,0,0,"Segoe UI");
        HGDIOBJ oldF = SelectObject(hdc, font);
        for (int i = 0; i < g_swCount; i++) {
            RECT tr = {g_swItems[i].rcDst.left, g_swItems[i].rcDst.bottom + 4,
                       g_swItems[i].rcDst.right, g_swItems[i].rcDst.bottom + TITLE_H};
            DrawTextA(hdc, g_swItems[i].title, -1, &tr,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        SelectObject(hdc, oldF); DeleteObject(font);

        /* 底部提示 */
        {
            HFONT hf = CreateFontA(-12,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,0,0,"Segoe UI");
            HGDIOBJ of = SelectObject(hdc, hf);
            SetTextColor(hdc, RGB(0xAA,0xAA,0xAA));
            RECT br = {MARGIN, rc.bottom-22, rc.right-MARGIN, rc.bottom-4};
            DrawTextA(hdc, "\xb7\xbd\xcf\xf2\xbc\xfc\xc7\xd0\xbb\xbb  Enter\xc8\xb7\xc8\xcf  Esc\xc8\xa1\xcf\xfb  \xcb\xab\xbb\xf7\xcb\xab\xbc\xfe\xd1\xa1\xd6\xd0",
                      -1, &br, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, of); DeleteObject(hf);
        }

        /* 主屏/副屏标签与分隔 */
        {
            int pCnt = 0; for (int i = 0; i < g_swCount; i++) if (g_swItems[i].isPrimary) pCnt++;
            int twoRows = (g_swCount > pCnt && pCnt > 0 &&
                (g_swCount * (THUMB_W + GAP) + MARGIN * 2) > (rc.right - rc.left));
            if (pCnt > 0 && pCnt < g_swCount) {
                if (twoRows) {
                    /* 双行：第一行上方标"主屏"，第二行上方标"副屏" */
                    RECT lbl1 = {MARGIN, 4, rc.right - MARGIN, MARGIN};
                    int lbl2y = MARGIN + THUMB_H + GAP_ROW/2 - 6;
                    RECT lbl2 = {MARGIN, lbl2y, rc.right - MARGIN, lbl2y + 22};
                    DrawTextA(hdc, "\xd6\xf7\xc6\xc1", -1, &lbl1, DT_CENTER | DT_TOP | DT_SINGLELINE);
                    DrawTextA(hdc, "\xb8\xb1\xc6\xc1", -1, &lbl2, DT_CENTER | DT_TOP | DT_SINGLELINE);
                } else {
                    /* 单行：左"主屏"，右"副屏"，中间分隔线 */
                    int sepX = (g_swItems[pCnt-1].rcDst.right + g_swItems[pCnt].rcDst.left) / 2;
                    RECT lL = {MARGIN, 4, sepX - 6, MARGIN};
                    RECT rL = {sepX + 6, 4, rc.right - MARGIN, MARGIN};
                    DrawTextA(hdc, "\xd6\xf7\xc6\xc1", -1, &lL, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE);
                    DrawTextA(hdc, "\xb8\xb1\xc6\xc1", -1, &rL, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
                    HPEN pen = CreatePen(PS_SOLID, SEP_W, RGB(0x55,0x55,0x55));
                    HGDIOBJ oldP = SelectObject(hdc, pen);
                    MoveToEx(hdc, sepX, 10, NULL);
                    LineTo(hdc, sepX, rc.bottom - 10);
                    SelectObject(hdc, oldP); DeleteObject(pen);
                }
            }
        }
        EndPaint(hWnd, &ps);
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        int mx = LOWORD(lParam), my = HIWORD(lParam);
        for (int i = 0; i < g_swCount; i++) {
            if (mx >= g_swItems[i].rcDst.left && mx <= g_swItems[i].rcDst.right &&
                my >= g_swItems[i].rcDst.top - 5 &&
                my <= g_swItems[i].rcDst.bottom + TITLE_H + 5) {
                g_swSel = i; InvalidateRect(hWnd, NULL, TRUE);
                /* 鼠标单击选中，双击确认 */
                break;
            }
        }
        return 0;
    }
    if (msg == WM_LBUTTONDBLCLK) {
        int mx = LOWORD(lParam), my = HIWORD(lParam);
        for (int i = 0; i < g_swCount; i++) {
            if (mx >= g_swItems[i].rcDst.left && mx <= g_swItems[i].rcDst.right &&
                my >= g_swItems[i].rcDst.top - 5 &&
                my <= g_swItems[i].rcDst.bottom + TITLE_H + 5) {
                g_swSel = i; AltTabConfirm();
                break;
            }
        }
        return 0;
    }
    if (msg == WM_KEYDOWN) {
        switch (wParam) {
        case VK_LEFT: case VK_UP:
            PostMessageA(g_hMainWnd, WM_USER + 201, -1, 0); return 0;
        case VK_RIGHT: case VK_DOWN:
            PostMessageA(g_hMainWnd, WM_USER + 201, 1, 0); return 0;
        case VK_RETURN: case VK_SPACE:
            PostMessageA(g_hMainWnd, WM_USER + 202, 0, 0); return 0;
        case VK_ESCAPE:
            PostMessageA(g_hMainWnd, WM_USER + 203, 0, 0); return 0;
        }
        return 0;
    }
    if (msg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) {
        PostMessageA(g_hMainWnd, WM_USER + 203, 0, 0);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

void AltTabShow(void) {
    if (!g_cfg.SwitcherEnabled) { g_switcherActive = 0; return; }
    g_swCount = 0;
    HWND hWnd = GetTopWindow(NULL);
    while (hWnd && g_swCount < 64) {
        if (IsWindowVisible(hWnd) && IsRealAppWindow(hWnd)) {
            char title[256];
            if (GetWindowTextA(hWnd, title, sizeof(title)) && title[0]) {
                HMONITOR hm = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONULL);
                g_swItems[g_swCount].hwnd = hWnd;
                g_swItems[g_swCount].thumb = NULL;
                strcpy(g_swItems[g_swCount].title, title);
                g_swItems[g_swCount].isPrimary = (hm && IsPrimaryMonitor(hm)) ? 1 : 0;
                g_swCount++;
            }
        }
        hWnd = GetNextWindow(hWnd, GW_HWNDNEXT);
    }
    if (g_swCount == 0) { g_switcherActive = 0; return; }

    /* 使用钩子中记录的光标位置确定目标屏幕 */
    {
        g_swTargetMon = MonitorFromPoint(g_swHookCursor, MONITOR_DEFAULTTONULL);
        if (!g_swTargetMon || g_swTargetMon == GetPrimaryMonitorHandle()) {
            /* 钩子位置无效或指向主屏 → 用当前光标位置重新检测 */
            POINT pt; GetCursorPos(&pt);
            g_swTargetMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
            if (!g_swTargetMon) g_swTargetMon = GetPrimaryMonitorHandle();
        }
    }
    RECT rcMon; GetMonitorRect(g_swTargetMon, &rcMon);

    /* 确定显示行数：单行或双行 */
    int pCnt = 0; for (int i = 0; i < g_swCount; i++) if (g_swItems[i].isPrimary) pCnt++;
    int sCnt = g_swCount - pCnt;
    int monW = rcMon.right - rcMon.left;
    int gapRow = GAP_ROW;
    int twoRows = (g_swCount * (THUMB_W + GAP) + MARGIN * 2 > monW) && pCnt > 0 && sCnt > 0;
    int itemsPerRow = twoRows ? (pCnt > sCnt ? pCnt : sCnt) : g_swCount;
    int maxFit = (monW - MARGIN * 2) / (THUMB_W + GAP);
    if (itemsPerRow > maxFit) itemsPerRow = maxFit;
    int totalW = itemsPerRow * (THUMB_W + GAP) + MARGIN * 2;
    int totalH = (twoRows ? 2 : 1) * (THUMB_H + gapRow) + TITLE_H + MARGIN * 2 + 20;
    int cx = rcMon.left + (monW - totalW) / 2;
    int cy = rcMon.top + (rcMon.bottom - rcMon.top - totalH) / 2;
    if (cx < rcMon.left + 5) cx = rcMon.left + 5;

    /* 注册窗口类 */
    static int reg = 0;
    if (!reg) {
        WNDCLASSA wc = {0};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = SwitcherWndProc;
        wc.hInstance = g_hInst;
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = "AltTabSwitcherClass";
        RegisterClassA(&wc); reg = 1;
    }

    g_swHwnd = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "AltTabSwitcherClass", NULL, WS_POPUP,
        cx, cy, totalW, totalH, NULL, NULL, g_hInst, NULL);
    if (!g_swHwnd) { g_switcherActive = 0; return; }

    /* 重排：主屏窗口在前，副屏在后（避免 Z-order 交叠导致 col 冲突） */
    {
        SwItem sorted[64]; int n = 0;
        for (int i = 0; i < g_swCount; i++) if (g_swItems[i].isPrimary) sorted[n++] = g_swItems[i];
        for (int i = 0; i < g_swCount; i++) if (!g_swItems[i].isPrimary) sorted[n++] = g_swItems[i];
        memcpy(g_swItems, sorted, sizeof(SwItem) * g_swCount);
    }

    int row1Y = MARGIN;
    int row2Y = MARGIN + THUMB_H + gapRow;
    int contentW = itemsPerRow * (THUMB_W + GAP) - GAP;
    int startX = MARGIN + (totalW - MARGIN * 2 - contentW) / 2;
    for (int i = 0; i < g_swCount; i++) {
        int rowIdx = twoRows ? (i < pCnt ? 0 : 1) : 0; /* 主屏row0, 副屏row1 */
        int colInRow = twoRows ? (rowIdx == 0 ? i : i - pCnt) : i;
        int rowY = rowIdx == 0 ? row1Y : row2Y;
        int x = startX + colInRow * (THUMB_W + GAP);
        g_swItems[i].rcDst = (RECT){x, rowY, x + THUMB_W, rowY + THUMB_H};
    }

    /* 注册 DWM 缩略图 */
    for (int i = 0; i < g_swCount; i++) {
        DwmRegisterThumbnail(g_swHwnd, g_swItems[i].hwnd, &g_swItems[i].thumb);
        if (g_swItems[i].thumb) {
            DWM_THUMBNAIL_PROPERTIES dtp;
            ZeroMemory(&dtp, sizeof(dtp));
            dtp.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_RECTDESTINATION | DWM_TNP_OPACITY;
            dtp.fVisible = TRUE;
            dtp.rcDestination = g_swItems[i].rcDst;
            dtp.opacity = 230;
            DwmUpdateThumbnailProperties(g_swItems[i].thumb, &dtp);
        }
    }

    g_swSel = 0;
    ShowWindow(g_swHwnd, SW_SHOW);
    SetForegroundWindow(g_swHwnd);
    SetFocus(g_swHwnd);
    InvalidateRect(g_swHwnd, NULL, TRUE);
}

void AltTabCycle(int dir) {
    g_swSel = (g_swSel + dir + g_swCount) % g_swCount;
    InvalidateRect(g_swHwnd, NULL, TRUE);
}

void AltTabCancel(void);
void AltTabConfirm(void) {
    if (g_swSel < 0 || g_swSel >= g_swCount) { AltTabCancel(); return; }
    HWND target = g_swItems[g_swSel].hwnd;
    if (g_swTargetMon) {
        HMONITOR hMonW = MonitorFromWindow(target, MONITOR_DEFAULTTONULL);
        if (hMonW && hMonW != g_swTargetMon) {
            MoveWindowToMonitor(target, g_swTargetMon, g_fullScreenMode);
        }
    }
    /* 使用 SwitchToThisWindow 激活目标窗口（兼容 explorer.exe 等跨进程场景） */
    SwitchToThisWindow(target, TRUE);
    AltTabCancel();
}

void AltTabCancel(void) {
    g_switcherActive = 0;
    if (g_swHwnd) {
        for (int i = 0; i < g_swCount; i++)
            if (g_swItems[i].thumb) DwmUnregisterThumbnail(g_swItems[i].thumb);
        DestroyWindow(g_swHwnd);
        g_swHwnd = NULL;
    }
}

/* ─── 窗口过程 ────────────────────────────────────── */
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_uShellHookMsg) {
        switch (wParam) {
        case HSHELL_WINDOWCREATED:   OnWindowCreated((HWND)lParam); break;
        case HSHELL_WINDOWACTIVATED: OnWindowActivated((HWND)lParam); break;
        case 2: /* HSHELL_WINDOWDESTROYED */
            g_lastDestroyTick = GetTickCount();
            break;
        }
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        g_uShellHookMsg = RegisterWindowMessageA("SHELLHOOK");
        RegisterShellHookWindow(hWnd);
        /* RegisterHotKey 替代键盘钩子，零系统开销，不影响输入法 */
        RegisterHotKey(hWnd, IDH_DISPLAY, MOD_WIN|MOD_ALT, 0x50); /* P */
        RegisterHotKey(hWnd, IDH_ORIENT, MOD_WIN|MOD_ALT, 0x4F);
        RegisterHotKey(hWnd, IDH_SWITCHER, MOD_WIN|MOD_SHIFT, VK_TAB);
        SetTimer(hWnd, IDT_INI_CHECK, 1000, NULL);
        return 0;

    case WM_USER + 200:  /* Show switcher */
        if (!g_switcherActive) {
            g_switcherActive = 1;
            PostMessageA(hWnd, WM_USER + 210, 0, 0);
        }
        return 0;
    case WM_USER + 201:  /* Cycle selection */
        if (g_switcherActive)
            AltTabCycle((int)wParam);
        return 0;
    case WM_USER + 202:  /* Confirm selection */
        if (g_switcherActive)
            AltTabConfirm();
        return 0;
    case WM_USER + 203:  /* Cancel */
        if (g_switcherActive)
            AltTabCancel();
        return 0;
    case WM_USER + 210:  /* Actually show the switcher UI */
        AltTabShow();
        return 0;

    case WM_HOTKEY: {
        int id = (int)wParam;
        if (id==IDH_DISPLAY) {int c=GetMonitorCount();
            if(c>=2)ShellExecuteA(0,"open","DisplaySwitch.exe","/internal",0,SW_HIDE);
            else{ShellExecuteA(0,"open","DisplaySwitch.exe","/extend",0,SW_HIDE);
            if(g_cfg.SavedBrightness>=0)SetSecondaryBrightness(g_cfg.SavedBrightness);
            SetOrientationAndSide(g_cfg.SecondaryOrientation,g_cfg.SecondarySide);}
            UpdateActiveState();return 0;
        }
        if(id==IDH_ORIENT&&GetMonitorCount()>=2){
            int n=(g_cfg.SecondaryOrientation+1)%4;
            if(SetOrientationAndSide(n,g_cfg.SecondarySide)){g_cfg.SecondaryOrientation=n;SaveConfig();}
            return 0;
        }
        if(id==IDH_SWITCHER&&!g_switcherActive){
            g_switcherActive=1;GetCursorPos(&g_swHookCursor);AltTabShow();return 0;
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == IDT_INI_CHECK) { CheckIniChanged(); return 0; }
        return 0;

    case WM_DESTROY:
        DestroyTrayIcon(hWnd);
        PostQuitMessage(0);
        return 0;

    case WM_TRAY_ICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuA(hMenu, MF_STRING, ID_TRAY_OPEN, "\xc9\xe8\xd6\xc3");
            AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "\xcd\xcb\xb3\xf6");
            SetForegroundWindow(hWnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                                     pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
            if (cmd == ID_TRAY_OPEN) OpenSettingsGui();
            else if (cmd == ID_TRAY_EXIT) PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

/* ─── WinMain ──────────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nCmdShow) {
    (void)hPrev;

    g_hInst = hInst;
    GetExeDir();
    GetIniPath();

    /* --settings 参数 → 直接运行设置界面 */
    if (lpCmd && lpCmd[0] && strstr(lpCmd, "--settings")) {
        SetProcessDPIAware();
        INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
        InitCommonControlsEx(&icc);
        return RunSettingsGui(hInst, nCmdShow);
    }

    SetProcessDPIAware();

    HANDLE hMutex = CreateMutexA(NULL, FALSE, "WindowMoveMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hW = FindWindowA("WindowMoveClass", NULL);
        if (hW) { ShowWindow(hW, SW_SHOW); SetForegroundWindow(hW); }
        CloseHandle(hMutex);
        return 0;
    }

    LoadConfig();
    UpdateActiveState();

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIconA(hInst, MAKEINTRESOURCEA(1));
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "WindowMoveClass";
    RegisterClassA(&wc);

    g_hMainWnd = CreateWindowA("WindowMoveClass", "Secondary Display Assistant",
                               0, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
    if (!g_hMainWnd) { CloseHandle(hMutex); return 1; }

    CreateTrayIcon(g_hMainWnd);

    if (GetMonitorCount() >= 2) {
        if (g_cfg.SavedBrightness >= 0)
            SetSecondaryBrightness(g_cfg.SavedBrightness);
        SetOrientationAndSide(g_cfg.SecondaryOrientation, g_cfg.SecondarySide);
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CloseHandle(hMutex);
    return 0;
}
