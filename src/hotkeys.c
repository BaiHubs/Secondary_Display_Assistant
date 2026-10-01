/* hotkeys.c — 全局热键
 *
 * 使用 RegisterHotKey 注册系统级热键（零系统开销、不影响输入法），
 * 并在 WM_HOTKEY 到达时执行对应动作：
 *   Win+Alt+P   切换显示模式（内屏 / 扩展）
 *   Win+Alt+O   循环切换副屏方向（0°/90°/180°/270°）
 *   Win+Shift+Tab  唤起窗口切换器
 */
#include "winmover.h"

/* 热键 ID（对应 RegisterHotKey 的 id 参数） */
#define IDH_DISPLAY   9001   /* Win+Alt+P */
#define IDH_SWITCHER  9002   /* Win+Shift+Tab */
#define IDH_ORIENT    9003   /* Win+Alt+O */

/* ─── 注册全部全局热键（WM_CREATE 时调用） ───────────── */
void RegisterAppHotKeys(HWND hWnd) {
    /* RegisterHotKey 替代键盘钩子，零系统开销，不影响输入法 */
    RegisterHotKey(hWnd, IDH_DISPLAY, MOD_WIN|MOD_ALT, 0x50);   /* P */
    RegisterHotKey(hWnd, IDH_ORIENT, MOD_WIN|MOD_ALT, 0x4F);    /* O */
    RegisterHotKey(hWnd, IDH_SWITCHER, MOD_WIN|MOD_SHIFT, VK_TAB);
}

/* ─── 处理 WM_HOTKEY ─────────────────────────────────── */
void HandleHotKey(int id) {
    if (id == IDH_DISPLAY) {
        /* 有副屏 → 切内屏；无副屏 → 切扩展，并应用副屏亮度/方向 */
        int c = GetMonitorCount();
        if (c >= 2) {
            ShellExecuteA(0, "open", "DisplaySwitch.exe", "/internal", 0, SW_HIDE);
        } else {
            ShellExecuteA(0, "open", "DisplaySwitch.exe", "/extend", 0, SW_HIDE);
            if (g_cfg.SavedBrightness >= 0) SetSecondaryBrightness(g_cfg.SavedBrightness);
            SetOrientationAndSide(g_cfg.SecondaryOrientation, g_cfg.SecondarySide);
        }
        UpdateActiveState();
        return;
    }
    if (id == IDH_ORIENT && GetMonitorCount() >= 2) {
        /* 依次循环 0→1→2→3→0，成功后写回配置 */
        int n = (g_cfg.SecondaryOrientation + 1) % 4;
        if (SetOrientationAndSide(n, g_cfg.SecondarySide)) {
            g_cfg.SecondaryOrientation = n;
            SaveConfig();
        }
        return;
    }
    if (id == IDH_SWITCHER) {
        /* 内部会在切换器已激活时直接返回 */
        SwitcherStartFromHotkey();
        return;
    }
}
