/* wndproc.c — 主窗口消息过程（消息分发中心）
 *
 * 主窗口本身不可见，只用于接收系统消息。WndProc 保持“薄分发”：
 *   - Shell 钩子消息     → shellhook.c  ShellHookHandleMessage()
 *   - 切换器自定义消息   → switcher.c   SwitcherHandleMessage()
 *   - WM_CREATE          → 注册 Shell 钩子 / 全局热键 / INI 轮询定时器
 *   - WM_HOTKEY          → hotkeys.c    HandleHotKey()
 *   - WM_TRAY_ICON       → tray.c       TrayHandleMessage()
 *   - WM_TIMER           → config.c     CheckIniChanged()
 *   - WM_DESTROY         → 移除托盘图标并退出消息循环
 * 其余消息交给 DefWindowProcA 默认处理。
 */
#include "winmover.h"

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    /* Shell 钩子消息（RegisterWindowMessage 动态生成的消息号） */
    if (ShellHookHandleMessage(msg, wParam, lParam)) return 0;

    /* 窗口切换器的自定义消息（WM_USER+200 系列） */
    if (SwitcherHandleMessage(msg, wParam, lParam)) return 0;

    switch (msg) {
    case WM_CREATE:
        ShellHookInit(hWnd);                       /* 注册 Shell 钩子窗口 */
        RegisterAppHotKeys(hWnd);                  /* 注册全局热键 */
        SetTimer(hWnd, IDT_INI_CHECK, 1000, NULL); /* 每秒轮询 INI 变更 */
        return 0;

    case WM_HOTKEY:
        HandleHotKey((int)wParam);
        return 0;

    case WM_TIMER:
        if (wParam == IDT_INI_CHECK) { CheckIniChanged(); return 0; }
        return 0;

    case WM_TRAY_ICON:
        TrayHandleMessage(hWnd, wParam, lParam);
        return 0;

    case WM_DISPLAYCHANGE:
        InvalidatePrimaryMonitor();   /* 分辨率/拓扑变化 → 主屏缓存失效 */
        return 0;

    case WM_DESTROY:
        DestroyTrayIcon(hWnd);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}
