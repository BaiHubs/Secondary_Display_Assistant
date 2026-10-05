/* shellhook.c — Shell 钩子（窗口创建 / 激活 / 销毁 通知）
 *
 * 通过 RegisterShellHookWindow 接收资源管理器广播的窗口事件：
 *   - HSHELL_WINDOWCREATED    新窗口创建   → 若光标在副屏则延迟移动过去
 *   - HSHELL_WINDOWACTIVATED  窗口激活     → 任务栏跨屏点亮时把窗口移到光标屏
 *   - HSHELL_RUDEAPPACTIVATED 全屏应用激活 → 同上（全屏应用走此消息）
 *   - HSHELL_WINDOWDESTROYED  窗口销毁     → 记录时刻用于防抖
 *
 * 参照 PowerToys GrabAndMove（ResolveTargetWindow / IsSystemClass / HandleDragMove）：
 *   - shell 钩子上报的窗口先“规范化为根顶层窗口”，并排除系统窗口
 *     （桌面 / 任务栏 / 溢出 / 提示 / 菜单 / 任务视图 / 系统浮出）；
 *   - 移动前先还原已最大化 / 最小化的窗口，否则 SetWindowPos 位置不生效。
 *
 * 窗口判定复用 window_filter.c 的 IsRealAppWindow()/IsExcludedWindow()，
 * 实际移动复用 move.c 的 MoveWindowToMonitor()/DelayedMove()。
 */
#include "winmover.h"

/* ─── Shell Hook 事件常量 ────────────────────────────── */
#define HSHELL_WINDOWCREATED     1
#define HSHELL_WINDOWDESTROYED   2
#define HSHELL_WINDOWACTIVATED   4
/* HSHELL_RUDEAPPACTIVATED 由 windows.h 提供（= 32772，全屏应用激活） */

/* ─── 内部状态 ───────────────────────────────────────── */
static UINT  g_uShellHookMsg      = 0;   /* SHELLHOOK 动态消息号 */
static DWORD g_lastActivationTick = 0;   /* 上次激活处理时刻（防抖） */
static DWORD g_lastDestroyTick    = 0;   /* 上次窗口销毁时刻（防抖） */

/* ─── 系统窗口类名（参照 GrabAndMove 的 IsSystemClass） ──
 * 这些窗口类不应被当作可移动的应用窗口：桌面/背景、主副任务栏、
 * 托盘溢出、提示气泡、弹出菜单、任务视图、系统浮出等。 */
static int IsSystemClass(HWND hWnd) {
    char cls[64] = {0};
    if (!GetClassNameA(hWnd, cls, sizeof(cls))) return 0;
    static const char *known[] = {
        "Progman", "WorkerW",                        /* 桌面 / 背景 */
        "Shell_TrayWnd", "Shell_SecondaryTrayWnd",    /* 主 / 副任务栏 */
        "NotifyIconOverflowWindow",                   /* 托盘溢出 */
        "TopLevelWindowForOverflowXamlIsland",
        "tooltips_class32",                           /* 提示气泡 */
        "#32768",                                     /* 弹出菜单 */
        "MultitaskingViewFrame",                      /* 任务视图 Win+Tab */
        "XamlExplorerHostIslandWindow",
        "Windows.UI.Composition.DesktopWindowContentBridge", /* 系统浮出 */
        "Shell_InputSwitchTopLevelWindow",            /* 输入法切换浮出 */
        NULL
    };
    for (int i = 0; known[i]; i++) {
        if (strcmp(cls, known[i]) == 0) return 1;
    }
    return 0;
}

/* ─── 把 shell 钩子窗口规范化为“根顶层窗口” ─────────────
 * shell 钩子有时上报子窗口；向上取根窗口，并排除本程序自身窗口
 * 与系统窗口。返回可移动的目标窗口，否则返回 NULL。 */
HWND ResolveMoveTarget(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return NULL;
    HWND root = GetAncestor(hWnd, GA_ROOT);
    if (root) hWnd = root;
    if (hWnd == g_guiHwnd || hWnd == g_hMainWnd) return NULL;
    if (IsSystemClass(hWnd)) return NULL;
    return hWnd;
}

/* ─── 移动前还原已最小化 / 最大化的窗口 ─────────────────
 * 最大化窗口直接 SetWindowPos 不会真正移动（仍贴合原屏），
 * 需先还原为普通窗口，位置才能生效（GrabAndMove 同此做法）。 */
static void RestoreWindowForMove(HWND hWnd) {
    if (IsIconic(hWnd)) {
        ShowWindow(hWnd, SW_RESTORE);
        Sleep(60);   /* 等最小化动画结束，以便取到还原后的矩形 */
    }
    if (IsZoomed(hWnd)) {
        ShowWindow(hWnd, SW_RESTORE);
        Sleep(30);
    }
}

/* ─── 鼠标是否在任务栏附近（含预览弹窗区域） ─────────── */
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

/* ─── 窗口创建回调：按光标所在屏移动该窗口 ───────────── */
static void OnWindowCreated(HWND hWnd) {
    if (!g_isActive) return;
    hWnd = ResolveMoveTarget(hWnd);       /* 规范化到根顶层窗口 */
    if (!hWnd) return;
    if (IsExcludedWindow(hWnd)) return;
    LONG s = GetWindowLongA(hWnd, GWL_STYLE);
    if (!(s & WS_CAPTION)) return;
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR hMonMouse = GetMonitorFromPointEx(pt.x, pt.y);
    DelayedMove(hWnd, hMonMouse);
}

/* ─── 窗口激活回调 ───────────────────────────────────── */
/* 简单可靠：用 shell hook 的 hWnd + GetForegroundWindow 回退验证 */
static void OnWindowActivated(HWND hWnd) {
    if (!g_isActive || !g_taskbarMoveEnabled || g_movingFlag) return;
    if (GetTickCount() - g_lastActivationTick < 300) return;
    if (GetTickCount() - g_lastDestroyTick < 200) return;

    /* 规范化：shell 钩子可能上报子窗口，统一取根顶层窗口；
       shell 钩子窗口与前台不一致时，以前台根窗口为准。 */
    hWnd = ResolveMoveTarget(hWnd);
    {
        HWND fgWnd = ResolveMoveTarget(GetForegroundWindow());
        if (fgWnd && hWnd != fgWnd) hWnd = fgWnd;
    }
    if (!hWnd || !IsRealAppWindow(hWnd)) return;
    if (!IsCursorNearTaskbar()) return;

    POINT pt;
    GetCursorPos(&pt);
    HMONITOR hMonMouse = GetMonitorFromPointEx(pt.x, pt.y);
    HMONITOR hMonWnd   = GetMonitorFromWindowEx(hWnd);
    if (hMonWnd == hMonMouse) return;

    g_lastActivationTick = GetTickCount();

    /* 移动前还原最小化 / 最大化窗口，否则 SetWindowPos 位置不生效 */
    RestoreWindowForMove(hWnd);

    g_movingFlag = 1;
    MoveWindowToMonitor(hWnd, hMonMouse, g_fullScreenMode);
    g_movingFlag = 0;
}

/* ─── 注册 Shell 钩子（WM_CREATE 时调用） ────────────── */
void ShellHookInit(HWND hWnd) {
    g_uShellHookMsg = RegisterWindowMessageA("SHELLHOOK");
    RegisterShellHookWindow(hWnd);
}

/* ─── 处理 Shell 钩子消息：返回非 0 表示已处理 ───────── */
int ShellHookHandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg != g_uShellHookMsg) return 0;
    switch (wParam) {
    case HSHELL_WINDOWCREATED:    OnWindowCreated((HWND)lParam); break;
    case HSHELL_WINDOWACTIVATED:
    case HSHELL_RUDEAPPACTIVATED: OnWindowActivated((HWND)lParam); break;
    case HSHELL_WINDOWDESTROYED:  g_lastDestroyTick = GetTickCount(); break;
    }
    return 1;
}
