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

/* ─── 移动窗口到目标显示器 ──────────────────────────── */
void MoveWindowToMonitor(HWND hWnd, HMONITOR hTarget, int fullScreen) {
    RECT rcMon;
    if (!GetMonitorRect(hTarget, &rcMon)) return;
    int mL = rcMon.left, mT = rcMon.top;
    int mR = rcMon.right, mB = rcMon.bottom;

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

    /* 非全屏模式：按 DPI 缩放 */
    HMONITOR hCurr = GetMonitorFromWindowEx(hWnd);
    ResizeWindowForMonitor(hWnd, hCurr, hTarget, &ww, &wh);

    int newX = mL + (mR - mL - ww) / 2;
    int newY = mT + (mB - mT - wh) / 2;
    if (newY < mT + 30) newY = mT + 30;
    if (newY + wh > mB) newY = mB - wh;
    if (newY < mT) newY = mT;

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
    if (!(style & 0xC00000) && !(style & 0x40000)) return;

    g_movingFlag = 1;
    MoveWindowToMonitor(hWnd, hMonMouse, g_fullScreenMode);
    g_movingFlag = 0;
}
