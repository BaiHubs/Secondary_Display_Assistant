/* move.c — 窗口移动
 *
 * 负责把窗口移动到目标显示器并按 DPI 缩放尺寸：
 *   - MoveWindowToMonitor()    移动到指定显示器（支持全屏模式）
 *   - DelayedMove()            窗口创建后的延迟移动
 *   - ResizeWindowForMonitor() 依据两屏缩放比例调整窗口大小（内部）
 * 窗口筛选（IsExcludedWindow / IsRealAppWindow）见 window_filter.c。
 */
#include "winmover.h"

/* ─── 根据 DPI 缩放窗口大小 ────────────────────────── */
static void ResizeWindowForMonitor(HWND hWnd, HMONITOR currMon, HMONITOR tgtMon,
                                   int *outW, int *outH) {
    int currScale = GetMonitorScale(currMon);
    int tgtScale  = GetMonitorScale(tgtMon);
    if (currScale == tgtScale) return;

    double factor = (double)tgtScale / (double)currScale;
    int newW = (int)(*outW * factor);
    int newH = (int)(*outH * factor);
    if (newW < 200) newW = 200;
    if (newH < 200) newH = 200;

    RECT rcMon;
    if (GetMonitorRect(tgtMon, &rcMon)) {
        int mw = rcMon.right - rcMon.left;
        int mh = rcMon.bottom - rcMon.top;
        if (newW > mw) newW = mw;
        if (newH > mh) newH = mh;
    }
    *outW = newW;
    *outH = newH;
}

/* ─── 移动前还原已最小化 / 最大化的窗口 ─────────────────
 * 最大化窗口直接 SetWindowPos 不会真正移动（仍贴合原屏），需先还原为普通窗口
 * 位置才生效（参照 GrabAndMove HandleDragMove）。切换器确认路径同样受益。 */
static void RestoreWindowForMove(HWND hWnd) {
    if (IsIconic(hWnd)) {
        ShowWindow(hWnd, SW_RESTORE);
        Sleep(60);   /* 等最小化动画结束，以便取到还原后的矩形 */
    } else if (IsZoomed(hWnd)) {
        ShowWindow(hWnd, SW_RESTORE);
        Sleep(30);
    }
}

/* ─── 移动窗口到目标显示器 ──────────────────────────── */
void MoveWindowToMonitor(HWND hWnd, HMONITOR hTarget, int fullScreen) {
    RECT rcMon;
    if (!GetMonitorRect(hTarget, &rcMon)) return;
    int mL = rcMon.left, mT = rcMon.top;
    int mR = rcMon.right, mB = rcMon.bottom;

    /* 先还原最小化 / 最大化，后面的 GetWindowRect 才能取到真实尺寸 */
    RestoreWindowForMove(hWnd);

    int isPrimaryTarget = IsPrimaryMonitor(hTarget);
    RECT rcWnd;
    GetWindowRect(hWnd, &rcWnd);
    int ww = rcWnd.right - rcWnd.left;
    int wh = rcWnd.bottom - rcWnd.top;
    if (ww <= 0) ww = 800;
    if (wh <= 0) wh = 600;

    /* 全屏模式：移到主屏还原，移到副屏最大化 */
    if (fullScreen && isPrimaryTarget) {
        SetWindowPos(hWnd, NULL,
                     mL + (mR - mL - ww) / 2,
                     mT + (mB - mT - wh) / 2,
                     ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    if (fullScreen && !isPrimaryTarget) {
        SetWindowPos(hWnd, NULL,
                     mL + (mR - mL - ww) / 2,
                     mT + (mB - mT - wh) / 2,
                     ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(hWnd, SW_MAXIMIZE);
        return;
    }

    /* 非全屏模式：按 DPI 缩放，并在“工作区”内居中 + 夹取（避开任务栏） */
    RECT rcWork;
    if (!GetMonitorWorkRect(hTarget, &rcWork)) rcWork = rcMon;
    int wL = rcWork.left, wT = rcWork.top, wR = rcWork.right, wB = rcWork.bottom;

    HMONITOR hCurr = GetMonitorFromWindowEx(hWnd);
    ResizeWindowForMonitor(hWnd, hCurr, hTarget, &ww, &wh);

    int newX = wL + (wR - wL - ww) / 2;
    int newY = wT + (wB - wT - wh) / 2;
    if (newY < wT + 30) newY = wT + 30;
    if (newY + wh > wB) newY = wB - wh;
    if (newY < wT) newY = wT;
    if (newX + ww > wR) newX = wR - ww;
    if (newX < wL) newX = wL;

    SetWindowPos(hWnd, NULL, newX, newY, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
}

/* ─── 延迟移动（用于窗口创建） ──────────────────────── */
void DelayedMove(HWND hWnd, HMONITOR hMonMouse) {
    if (!g_isActive) return;
    if (!IsWindow(hWnd)) return;
    if (IsExcludedWindow(hWnd)) return;

    HMONITOR hCurr = GetMonitorFromWindowEx(hWnd);
    if (hCurr == hMonMouse) return;

    LONG style = GetWindowLongA(hWnd, GWL_STYLE);
    if (!(style & WS_CAPTION) && !(style & WS_THICKFRAME)) return;

    g_movingFlag = 1;
    MoveWindowToMonitor(hWnd, hMonMouse, g_fullScreenMode);
    g_movingFlag = 0;
}
