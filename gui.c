/* gui.c — 托盘图标 */
#include "winmover.h"

static NOTIFYICONDATAA g_nid;

void CreateTrayIcon(HWND hWnd) {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd   = hWnd;
    g_nid.uID    = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY_ICON;
    g_nid.hIcon  = LoadIconA(g_hInst, MAKEINTRESOURCEA(1));
    strcpy(g_nid.szTip, "Secondary Display Assistant");
    Shell_NotifyIconA(NIM_ADD, &g_nid);
}

void DestroyTrayIcon(HWND hWnd) {
    (void)hWnd;
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
}
