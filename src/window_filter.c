/* window_filter.c — 窗口筛选 / 分类
 *
 * 提供两类窗口判定，供 Shell 钩子与切换器共用：
 *   - IsExcludedWindow()  排除临时窗口 / 工具窗口 / 无边框小窗 / 已知类名
 *   - IsRealAppWindow()   在排除基础上，进一步要求“有可见标题栏 + 非空标题
 *                          + 非桌面/任务栏 + 非系统进程”，即本程序可管理的
 *                          真实应用窗口（保留文件资源管理器）。
 */
#include "winmover.h"

/* ─── 系统窗口类名判定（供筛选与 Shell 钩子共用） ───────
 * 这些窗口类不应被当作可移动的应用窗口：桌面/背景、主副任务栏、托盘溢出与
 * 提示、弹出菜单、任务视图、系统浮出、截图/取色/工具窗等。
 * 注意：UWP 应用的最外层类 ApplicationFrameWindow **刻意不在此列** —— 它是
 * 设置/计算器等 UWP 应用的宿主，应可移动（参照 GrabAndMove：仅按进程路径过滤
 * shell 体验窗口，而非整类排除）。 */
int IsSystemClass(HWND hWnd) {
    char cls[64] = {0};
    if (!GetClassNameA(hWnd, cls, sizeof(cls))) return 0;
    static const char *known[] = {
        /* 桌面 / 背景 */
        "Progman", "WorkerW",
        /* 主 / 副任务栏 */
        "Shell_TrayWnd", "Shell_SecondaryTrayWnd",
        /* 托盘溢出 / 提示气泡 */
        "NotifyIconOverflowWindow", "TopLevelWindowForOverflowXamlIsland",
        "tooltips_class32",
        /* 弹出菜单 */
        "#32768",
        /* 任务视图 Win+Tab */
        "MultitaskingViewFrame", "XamlExplorerHostIslandWindow",
        /* 系统浮出（快速设置 / 输入法切换） */
        "Windows.UI.Composition.DesktopWindowContentBridge",
        "Shell_InputSwitchTopLevelWindow",
        /* 截图 / 取色 / 工具窗 / 输入法候选（原 IsExcludedWindow 清单） */
        "Crosshair", "SnipWindow", "GDI+ Hook Window Class",
        "CiceroUIWndFrame", "Qt5QWindowIcon",
        "Windows.UI.Core.CoreWindow",
        "Intermediate D3D Window", "OverlayWindow",
        "PopupHost", "ScreenClippingHost",
        NULL
    };
    for (int i = 0; known[i]; i++) {
        if (strcmp(cls, known[i]) == 0) return 1;
    }
    return 0;
}

/* ─── 排除临时窗口 ────────────────────────────────────── */
int IsExcludedWindow(HWND hWnd) {
    if (!IsWindow(hWnd)) return 1;
    if (hWnd == g_guiHwnd) return 1;

    LONG style = GetWindowLongA(hWnd, GWL_STYLE);
    LONG exStyle = GetWindowLongA(hWnd, GWL_EXSTYLE);

    /* WS_EX_TOOLWINDOW */
    if (exStyle & WS_EX_TOOLWINDOW) return 1;
    /* WS_EX_NOACTIVATE + 无标题栏 */
    if ((exStyle & WS_EX_NOACTIVATE) && !(style & WS_CAPTION)) return 1;
    /* WS_EX_TRANSPARENT */
    if (exStyle & WS_EX_TRANSPARENT) return 1;
    /* 无标题栏且太小 */
    if (!(style & WS_CAPTION)) {
        RECT rc;
        if (GetWindowRect(hWnd, &rc)) {
            if ((rc.right - rc.left) < 100 && (rc.bottom - rc.top) < 100)
                return 1;
        }
    }

    /* 系统类名排除 */
    if (IsSystemClass(hWnd)) return 1;
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

    /* 排除系统进程（保留 explorer.exe — 文件资源管理器可移动；shell 体验窗口
       如开始菜单/搜索/Widgets 按进程路径排除，参照 GrabAndMove 的 IsExcluded） */
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
                _stricmp(fn, "shellexperiencehost.exe") == 0 ||
                _stricmp(fn, "startmenuexperiencehost.exe") == 0 ||
                _stricmp(fn, "searchhost.exe") == 0 ||
                _stricmp(fn, "shellhost.exe") == 0 ||
                _stricmp(fn, "widgetboard.exe") == 0) {
                CloseHandle(hp);
                return 0;
            }
        }
        CloseHandle(hp);
    }
    return 1;
}
