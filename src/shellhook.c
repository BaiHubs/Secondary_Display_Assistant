/* shellhook.c — Shell 钩子（窗口创建 / 激活 / 销毁 通知）
 *
 * 通过 RegisterShellHookWindow 接收资源管理器广播的窗口事件：
 *   - HSHELL_WINDOWCREATED   新窗口创建 → 若光标在副屏则延迟移动过去
 *   - HSHELL_WINDOWACTIVATED 窗口激活   → 任务栏跨屏点亮时把窗口移到光标屏
 *   - HSHELL_WINDOWDESTROYED 窗口销毁   → 记录时刻用于防抖
 *
 * 窗口判定复用 window_filter.c 的 IsRealAppWindow()/IsExcludedWindow()，
 * 实际移动复用 move.c 的 MoveWindowToMonitor()/DelayedMove()。
 */
#include "winmover.h"

/* ─── Shell Hook 事件常量 ────────────────────────────── */
#define HSHELL_WINDOWCREATED   1
#define HSHELL_WINDOWACTIVATED 4
#define HSHELL_WINDOWDESTROYED 2

/* ─── 内部状态 ───────────────────────────────────────── */
static UINT  g_uShellHookMsg      = 0;   /* SHELLHOOK 动态消息号 */
static DWORD g_lastActivationTick = 0;   /* 上次激活处理时刻（防抖） */
static DWORD g_lastDestroyTick    = 0;   /* 上次窗口销毁时刻（防抖） */

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
    if (!IsWindow(hWnd)) return;
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

/* ─── 注册 Shell 钩子（WM_CREATE 时调用） ────────────── */
void ShellHookInit(HWND hWnd) {
    g_uShellHookMsg = RegisterWindowMessageA("SHELLHOOK");
    RegisterShellHookWindow(hWnd);
}

/* ─── 处理 Shell 钩子消息：返回非 0 表示已处理 ───────── */
int ShellHookHandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg != g_uShellHookMsg) return 0;
    switch (wParam) {
    case HSHELL_WINDOWCREATED:   OnWindowCreated((HWND)lParam); break;
    case HSHELL_WINDOWACTIVATED: OnWindowActivated((HWND)lParam); break;
    case HSHELL_WINDOWDESTROYED: g_lastDestroyTick = GetTickCount(); break;
    }
    return 1;
}
