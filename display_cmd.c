/* display_cmd.c — 副屏方向控制（内联，无需外部 display_helper.exe） */
#include "winmover.h"

/* ─── 获取副屏设备名（非主屏的那个） ───────────────── */
static char *GetSecondaryDeviceName(void) {
    static char devName[32];
    DISPLAY_DEVICEA dd; dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesA(NULL, i, &dd, 0); i++) {
        if (!(dd.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;
        if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
        DEVMODEA dm; ZeroMemory(&dm, sizeof(dm)); dm.dmSize = sizeof(dm); dm.dmDriverExtra = 0;
        if (!EnumDisplaySettingsA(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) continue;
        if (dm.dmPosition.x != 0 || dm.dmPosition.y != 0) {
            strcpy(devName, dd.DeviceName);
            return devName;
        }
    }
    return NULL;
}

/* ─── 获取主屏设备名 ──────────────────────────────── */
static char *GetPrimaryDeviceName(void) {
    static char devName[32];
    DISPLAY_DEVICEA dd; dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesA(NULL, i, &dd, 0); i++) {
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
            strcpy(devName, dd.DeviceName);
            return devName;
        }
    }
    return NULL;
}

/* ─── 获取当前副屏方向 ────────────────────────────── */
int GetCurrentOrientation(void) {
    char *name = GetSecondaryDeviceName();
    if (!name) return -1;
    DEVMODEA dm; ZeroMemory(&dm, sizeof(dm)); dm.dmSize = sizeof(dm); dm.dmDriverExtra = 0;
    if (!EnumDisplaySettingsA(name, ENUM_CURRENT_SETTINGS, &dm)) return -1;
    return (int)dm.dmDisplayOrientation;
}

/* ─── 设置副屏方向和侧边位置 ───────────────────────── */
int SetOrientationAndSide(int orientation, int side) {
    char *devName = GetSecondaryDeviceName();
    char *primName = GetPrimaryDeviceName();
    if (!devName || !primName) return 0;

    DEVMODEA primMode; ZeroMemory(&primMode, sizeof(primMode));
    primMode.dmSize = sizeof(primMode); primMode.dmDriverExtra = 0;
    if (!EnumDisplaySettingsA(primName, ENUM_CURRENT_SETTINGS, &primMode)) return 0;

    DEVMODEA curMode; ZeroMemory(&curMode, sizeof(curMode));
    curMode.dmSize = sizeof(curMode); curMode.dmDriverExtra = 0;
    if (!EnumDisplaySettingsA(devName, ENUM_CURRENT_SETTINGS, &curMode)) return 0;

    /* 反推物理分辨率 */
    DWORD physW, physH;
    int curOri = (int)curMode.dmDisplayOrientation;
    if (curOri == 1 || curOri == 3) { physW = curMode.dmPelsHeight; physH = curMode.dmPelsWidth; }
    else { physW = curMode.dmPelsWidth; physH = curMode.dmPelsHeight; }

    DEVMODEA newMode; ZeroMemory(&newMode, sizeof(newMode));
    newMode.dmSize = sizeof(newMode); newMode.dmDriverExtra = 0;
    if (!EnumDisplaySettingsA(devName, ENUM_CURRENT_SETTINGS, &newMode)) return 0;

    newMode.dmDisplayOrientation = (DWORD)orientation;
    if (orientation == 1 || orientation == 3) { newMode.dmPelsWidth = physH; newMode.dmPelsHeight = physW; }
    else { newMode.dmPelsWidth = physW; newMode.dmPelsHeight = physH; }

    if (side == 0) newMode.dmPosition.x = -(int)newMode.dmPelsWidth;
    else           newMode.dmPosition.x = (int)primMode.dmPelsWidth;
    newMode.dmPosition.y = 0;
    newMode.dmFields = DM_DISPLAYORIENTATION | DM_PELSWIDTH | DM_PELSHEIGHT | DM_POSITION;

    LONG r = ChangeDisplaySettingsExA(devName, &newMode, NULL, CDS_TEST, NULL);
    if (r != DISP_CHANGE_SUCCESSFUL) return 0;

    r = ChangeDisplaySettingsExA(devName, &newMode, NULL,
                                 CDS_UPDATEREGISTRY | CDS_RESET, NULL);
    if (r == DISP_CHANGE_SUCCESSFUL) {
        Sleep(500);
        NotifyWinEvent(0x8000, NULL, 0, 0);
        return 1;
    }
    return 0;
}
