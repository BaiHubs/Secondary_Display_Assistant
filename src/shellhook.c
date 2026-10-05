/* shellhook.c — Shell 钩子（窗口创建 / 激活 / 销毁 通知）
 *
 * 通过 RegisterShellHookWindow 接收资源管理器广播的窗口事件：
 *   - HSHELL_WINDOWCREATED    新窗口创建   → 若光标在副屏则延迟移动过去
 *   - HSHELL_WINDOWACTIVATED  窗口激活     → 任务栏跨屏点亮时把窗口移到光标屏
 *   - HSHELL_RUDEAPPACTIVATED 全屏应用激活 → 同上（全屏应用走此消息）
 *   - EVENT_SYSTEM_FOREGROUND 前台切换     → 任务栏点击跨屏移动的主触发（WinEvent）
 *   - HSHELL_WINDOWDESTROYED  窗口销毁     → 记录时刻用于防抖
 *
 * 参照 PowerToys GrabAndMove（ResolveTargetWindow / IsSystemClass / HandleDragMove）：
 *   - shell 钩子上报的窗口先“规范化为根顶层窗口”，并排除系统窗口
 *     （桌面 / 任务栏 / 溢出 / 提示 / 菜单 / 任务视图 / 系统浮出）；
 *   - 移动前先还原已最大化 / 最小化的窗口（由 move.c 的 MoveWindowToMonitor 内部完成）。
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
static UINT  g_uShellHookMsg       = 0;    /* SHELLHOOK 动态消息号 */
static DWORD g_lastActivationTick   = 0;   /* 上次激活处理时刻（防抖） */
static DWORD g_lastDestroyTick      = 0;   /* 上次窗口销毁时刻（防抖） */
static HWINEVENTHOOK g_hForegroundHook = NULL;  /* EVENT_SYSTEM_FOREGROUND 钩子 */

/* ─── 把 shell 钩子窗口规范化为“根顶层窗口” ─────────────
 * shell 钩子有时上报子窗口；向上取根窗口，并排除本程序自身窗口
 * 与系统窗口（IsSystemClass 见 window_filter.c）。返回可移动的
 * 目标窗口，否则返回 NULL。 */
HWND ResolveMoveTarget(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return NULL;
    HWND root = GetAncestor(hWnd, GA_ROOT);
    if (root) hWnd = root;
    if (hWnd == g_guiHwnd || hWnd == g_hMainWnd) return NULL;
    if (IsSystemClass(hWnd)) return NULL;
    return hWnd;
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
/* 把“新前台窗口”移到光标所在屏。触发源：shell 钩子 WINDOWACTIVATED 与
   EVENT_SYSTEM_FOREGROUND。传入的 hWnd 即被激活 / 新前台窗口，直接使用；
   不再用 GetForegroundWindow 覆盖——旧写法会在点击任务栏时被“尚未翻转的前台”
   （上一个窗口）覆盖，从而对错窗口判定同屏而永不移动。 */
static void TryMoveForegroundToCursorMonitor(HWND hWnd) {
    if (!g_isActive || !g_taskbarMoveEnabled || g_movingFlag) return;
    if (GetTickCount() - g_lastActivationTick < 300) return;
    if (GetTickCount() - g_lastDestroyTick < 200) return;

    hWnd = ResolveMoveTarget(hWnd);
    if (!hWnd || !IsRealAppWindow(hWnd)) return;
    if (!IsCursorNearTaskbar()) return;

    POINT pt;
    GetCursorPos(&pt);
    HMONITOR hMonMouse = GetMonitorFromPointEx(pt.x, pt.y);
    HMONITOR hMonWnd   = GetMonitorFromWindowEx(hWnd);
    if (hMonWnd == hMonMouse) return;

    g_lastActivationTick = GetTickCount();

    /* 还原最小化 / 最大化由 MoveWindowToMonitor 内部完成 */
    g_movingFlag = 1;
    MoveWindowToMonitor(hWnd, hMonMouse, g_fullScreenMode);
    g_movingFlag = 0;
}

/* EVENT_SYSTEM_FOREGROUND 回调（窗口激活主触发）：与 shell 钩子不同，
   WinEvent 回调拿到的 hwnd 就是“新前台窗口”，时机可靠（点击任务栏 / Alt+Tab /
   全屏切换都会触发，参照 GrabAndMove 用 WinEvent 捉前台）。 */
static void CALLBACK ForegroundEventProc(HWINEVENTHOOK hWinEventHook, DWORD event,
                                         HWND hWnd, LONG idObject, LONG idChild,
                                         DWORD dwEventThread, DWORD dwmsEventTime) {
    (void)hWinEventHook; (void)dwEventThread; (void)dwmsEventTime;
    if (event != EVENT_SYSTEM_FOREGROUND) return;
    if (idObject != OBJID_WINDOW || idChild != 0) return;
    if (!hWnd) return;
    TryMoveForegroundToCursorMonitor(hWnd);
}

/* ─── 注册 Shell 钩子（WM_CREATE 时调用） ────────────── */
void ShellHookInit(HWND hWnd) {
    g_uShellHookMsg = RegisterWindowMessageA("SHELLHOOK");
    RegisterShellHookWindow(hWnd);
    /* 前台切换钩子：任务栏点击跨屏移动的主触发。
       WINEVENT_OUTOFCONTEXT 回调在本线程消息循环中派发，无需额外线程。 */
    g_hForegroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                        NULL, ForegroundEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
}

/* 卸载 Shell 钩子（WM_DESTROY 时调用） */
void ShellHookUninit(void) {
    if (g_hForegroundHook) {
        UnhookWinEvent(g_hForegroundHook);
        g_hForegroundHook = NULL;
    }
    /* shell 钩子注册随窗口销毁自动解除，无需显式注销 */
}

/* ─── 处理 Shell 钩子消息：返回非 0 表示已处理 ───────── */
int ShellHookHandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg != g_uShellHookMsg) return 0;
    switch (wParam) {
    case HSHELL_WINDOWCREATED:    OnWindowCreated((HWND)lParam); break;
    case HSHELL_WINDOWACTIVATED:
    case HSHELL_RUDEAPPACTIVATED: TryMoveForegroundToCursorMonitor((HWND)lParam); break;
    case HSHELL_WINDOWDESTROYED:  g_lastDestroyTick = GetTickCount(); break;
    }
    return 1;
}
