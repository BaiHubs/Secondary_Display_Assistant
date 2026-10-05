/* monitor.c — 显示器查询
 *
 * 封装 Win32 多显示器相关查询：
 *   - GetMonitorCount()           显示器数量
 *   - GetPrimaryMonitorHandle()   主屏句柄（枚举查找）
 *   - IsPrimaryMonitor()          是否主屏
 *   - GetMonitorFromWindowEx/PointEx  由窗口/坐标取最近显示器
 *   - GetMonitorRect/WorkRect     显示器全区域 / 工作区域
 *   - GetMonitorScale()           显示器对应的 DPI 缩放百分比
 */
#include "winmover.h"

int GetMonitorCount(void) {
    return GetSystemMetrics(SM_CMONITORS);
}

/* ─── 枚举回调 ────────────────────────────────────── */
typedef struct {
    HMONITOR result;
    int      findPrimary;
} MonEnumCtx;

static BOOL CALLBACK EnumFindMon(HMONITOR hMon, HDC hdc, LPRECT rc, LPARAM lp) {
    (void)hdc; (void)rc;
    MonEnumCtx *ctx = (MonEnumCtx *)lp;
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfoA(hMon, &mi)) return TRUE;
    if (ctx->findPrimary) {
        if (mi.dwFlags & MONITORINFOF_PRIMARY) {
            ctx->result = hMon;
            return FALSE;
        }
    }
    return TRUE;
}

/* 主屏句柄缓存：避免 IsPrimaryMonitor()/GetMonitorScale() 每次重新枚举。
   显示器拓扑变化时由 InvalidatePrimaryMonitor() 失效
   （已接入 UpdateActiveState() 与 WM_DISPLAYCHANGE）。 */
static HMONITOR g_primaryMon    = NULL;
static int      g_primaryCached = 0;

void InvalidatePrimaryMonitor(void) {
    g_primaryMon    = NULL;
    g_primaryCached = 0;
}

HMONITOR GetPrimaryMonitorHandle(void) {
    if (g_primaryCached) return g_primaryMon;
    MonEnumCtx ctx = {NULL, 1};
    EnumDisplayMonitors(NULL, NULL, EnumFindMon, (LPARAM)&ctx);
    g_primaryMon    = ctx.result;
    g_primaryCached = 1;
    return g_primaryMon;
}

int IsPrimaryMonitor(HMONITOR hMon) {
    return hMon == GetPrimaryMonitorHandle();
}

HMONITOR GetMonitorFromWindowEx(HWND hWnd) {
    return MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
}

HMONITOR GetMonitorFromPointEx(int x, int y) {
    POINT pt = {x, y};
    return MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
}

int GetMonitorRect(HMONITOR hMon, RECT *rc) {
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfoA(hMon, &mi)) return 0;
    *rc = mi.rcMonitor;
    return 1;
}

int GetMonitorWorkRect(HMONITOR hMon, RECT *rc) {
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfoA(hMon, &mi)) return 0;
    *rc = mi.rcWork;
    return 1;
}

int GetMonitorScale(HMONITOR hMon) {
    if (IsPrimaryMonitor(hMon))
        return g_primaryScale;
    else
        return g_secondaryScale;
}
