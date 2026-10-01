/* brightness.c — DDC/CI 亮度控制（动态加载 dxva2.dll）
 *
 * 通过 DDC/CI 读写副屏亮度：
 *   - LoadDxva()                运行时加载 dxva2.dll 并取函数指针
 *   - GetSecondaryBrightness()  读取副屏当前亮度
 *   - SetSecondaryBrightness()  设置副屏亮度
 * 仅作用于非主屏显示器（回调中过滤 MONITORINFOF_PRIMARY）。
 */
#include "winmover.h"

#pragma pack(push, 8)
typedef struct {
    HANDLE hPhysicalMonitor;
    WCHAR  szPhysicalMonitorDescription[128];
} DDC_PHYSICAL_MONITOR;
#pragma pack(pop)

static BOOL (WINAPI *pGetNumPhysMons)(HMONITOR, LPDWORD);
static BOOL (WINAPI *pGetPhysMons)(HMONITOR, DWORD, DDC_PHYSICAL_MONITOR *);
static BOOL (WINAPI *pDestroyPhysMon)(HANDLE);
static BOOL (WINAPI *pGetMonBrightness)(HANDLE, LPDWORD, LPDWORD, LPDWORD);
static BOOL (WINAPI *pSetMonBrightness)(HANDLE, DWORD);
static int  g_dxvaLoaded = 0;

static int LoadDxva(void) {
    if (g_dxvaLoaded) return g_dxvaLoaded;
    HMODULE hMod = LoadLibraryA("dxva2.dll");
    if (!hMod) { g_dxvaLoaded = -1; return -1; }
    pGetNumPhysMons = (void*)GetProcAddress(hMod, "GetNumberOfPhysicalMonitorsFromHMONITOR");
    pGetPhysMons    = (void*)GetProcAddress(hMod, "GetPhysicalMonitorsFromHMONITOR");
    pDestroyPhysMon = (void*)GetProcAddress(hMod, "DestroyPhysicalMonitor");
    pGetMonBrightness = (void*)GetProcAddress(hMod, "GetMonitorBrightness");
    pSetMonBrightness = (void*)GetProcAddress(hMod, "SetMonitorBrightness");
    if (pGetNumPhysMons && pGetPhysMons && pDestroyPhysMon && pGetMonBrightness && pSetMonBrightness)
        { g_dxvaLoaded = 1; return 1; }
    g_dxvaLoaded = -1; return -1;
}

static BOOL CALLBACK GetBrightnessCB(HMONITOR hMon, HDC, LPRECT, LPARAM lp) {
    int *result = (int *)lp;
    MONITORINFOEXA mi = {0}; mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(hMon, (LPMONITORINFO)&mi) || (mi.dwFlags & MONITORINFOF_PRIMARY))
        return TRUE;
    DWORD num = 0;
    if (!pGetNumPhysMons(hMon, &num) || num == 0) return TRUE;
    DDC_PHYSICAL_MONITOR *pm = (DDC_PHYSICAL_MONITOR *)LocalAlloc(LPTR, sizeof(DDC_PHYSICAL_MONITOR) * num);
    if (!pm || !pGetPhysMons(hMon, num, pm)) { LocalFree(pm); return TRUE; }
    for (DWORD i = 0; i < num; i++) {
        DWORD cur = 0, min = 0, max = 0;
        if (pGetMonBrightness(pm[i].hPhysicalMonitor, &cur, &min, &max))
            *result = (int)cur;
        pDestroyPhysMon(pm[i].hPhysicalMonitor);
    }
    LocalFree(pm);
    return TRUE;
}

int GetSecondaryBrightness(void) {
    if (LoadDxva() < 0) return -1;
    int result = -1;
    EnumDisplayMonitors(NULL, NULL, GetBrightnessCB, (LPARAM)&result);
    return result;
}

static BOOL CALLBACK SetBrightnessCB(HMONITOR hMon, HDC, LPRECT, LPARAM lp) {
    int val = (int)(INT_PTR)lp;
    MONITORINFOEXA mi = {0}; mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(hMon, (LPMONITORINFO)&mi) || (mi.dwFlags & MONITORINFOF_PRIMARY))
        return TRUE;
    DWORD num = 0;
    if (!pGetNumPhysMons(hMon, &num) || num == 0) return TRUE;
    DDC_PHYSICAL_MONITOR *pm = (DDC_PHYSICAL_MONITOR *)LocalAlloc(LPTR, sizeof(DDC_PHYSICAL_MONITOR) * num);
    if (!pm || !pGetPhysMons(hMon, num, pm)) { LocalFree(pm); return TRUE; }
    for (DWORD i = 0; i < num; i++) {
        pSetMonBrightness(pm[i].hPhysicalMonitor, (DWORD)val);
        pDestroyPhysMon(pm[i].hPhysicalMonitor);
    }
    LocalFree(pm);
    return TRUE;
}

void SetSecondaryBrightness(int val) {
    if (LoadDxva() < 0) return;
    EnumDisplayMonitors(NULL, NULL, SetBrightnessCB, (LPARAM)(INT_PTR)val);
}
