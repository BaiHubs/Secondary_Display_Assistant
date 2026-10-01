/* window_filter.c — 窗口筛选 / 分类
 *
 * 提供两类窗口判定，供 Shell 钩子与切换器共用：
 *   - IsExcludedWindow()  排除临时窗口 / 工具窗口 / 无边框小窗 / 已知类名
 *   - IsRealAppWindow()   在排除基础上，进一步要求“有可见标题栏 + 非空标题
 *                          + 非桌面/任务栏 + 非系统进程”，即本程序可管理的
 *                          真实应用窗口（保留文件资源管理器）。
 */
#include "winmover.h"

/* ─── 排除临时窗口 ────────────────────────────────────── */
int IsExcludedWindow(HWND hWnd) {
    if (!IsWindow(hWnd)) return 1;
    if (hWnd == g_guiHwnd) return 1;

    LONG style = GetWindowLongA(hWnd, GWL_STYLE);
    LONG exStyle = GetWindowLongA(hWnd, GWL_EXSTYLE);

    /* WS_EX_TOOLWINDOW */
    if (exStyle & 0x80) return 1;
    /* WS_EX_NOACTIVATE + 无标题栏 */
    if ((exStyle & 0x80000) && !(style & 0xC00000)) return 1;
    /* WS_EX_TRANSPARENT */
    if (exStyle & 0x20) return 1;
    /* 无标题栏且太小 */
    if (!(style & 0xC00000)) {
        RECT rc;
        if (GetWindowRect(hWnd, &rc)) {
            if ((rc.right - rc.left) < 100 && (rc.bottom - rc.top) < 100)
                return 1;
        }
    }

    /* 已知类名排除 */
    char cls[64];
    if (GetClassNameA(hWnd, cls, sizeof(cls))) {
        static const char *known[] = {
            "Crosshair", "SnipWindow", "GDI+ Hook Window Class",
            "CiceroUIWndFrame", "Qt5QWindowIcon",
            "Windows.UI.Core.CoreWindow", "ApplicationFrameWindow",
            "Intermediate D3D Window", "OverlayWindow",
            "PopupHost", "ScreenClippingHost", NULL
        };
        for (int i = 0; known[i]; i++) {
            if (strcmp(cls, known[i]) == 0) return 1;
        }
    }
    return 0;
}

/* ─── 判定“真实应用窗口” ─────────────────────────────── */
int IsRealAppWindow(HWND hWnd) {
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
