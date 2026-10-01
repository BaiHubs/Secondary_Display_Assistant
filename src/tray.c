/* tray.c — 系统托盘图标与菜单
 *
 * 管理任务栏通知区图标，并处理其鼠标消息（WM_TRAY_ICON）：
 *   - CreateTrayIcon() / DestroyTrayIcon()  添加 / 移除图标
 *   - TrayHandleMessage()                    弹出菜单（设置 / 退出）
 * 图标资源为 winmover.rc 中的 ID 1。
 */
#include "winmover.h"

static NOTIFYICONDATAA g_nid;

/* ─── 添加托盘图标 ───────────────────────────────────── */
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

/* ─── 移除托盘图标 ───────────────────────────────────── */
void DestroyTrayIcon(HWND hWnd) {
    (void)hWnd;
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
}

/* ─── 处理托盘鼠标消息：弹出菜单 ─────────────────────── */
void TrayHandleMessage(HWND hWnd, WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
        POINT pt;
        GetCursorPos(&pt);
        HMENU hMenu = CreatePopupMenu();
        AppendMenuA(hMenu, MF_STRING, ID_TRAY_OPEN, "\xc9\xe8\xd6\xc3");
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "\xcd\xcb\xb3\xf6");
        SetForegroundWindow(hWnd);
        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                                 pt.x, pt.y, 0, hWnd, NULL);
        DestroyMenu(hMenu);
        if (cmd == ID_TRAY_OPEN) OpenSettingsGui();
        else if (cmd == ID_TRAY_EXIT) PostQuitMessage(0);
    }
}
