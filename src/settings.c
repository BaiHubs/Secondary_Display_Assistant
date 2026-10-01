/* settings.c — 设置界面（Core UI .uix 页面 + 原生桥接）
 *
 * 界面本体由 Core UI 框架渲染：布局与样式在 settings.uix 单文件组件里，
 * 本文件只做"宿主 + 桥接"三件事：
 *   1. 初始化 Core UI、加载 .uix、打开窗口；
 *   2. 打开后用 ui_widget_find_by_id 解析各控件句柄并挂原生回调；
 *   3. 回调里读写 g_cfg、调用原生操作（副屏方向 / 亮度），并写回 INI。
 *
 * 约定：交互控件的真实值由 C 端直接读写（不依赖 JS 双向绑定），
 * JS 侧 <script> 只负责状态行文字与"启用窗口移动"的 :enabled 联动；
 * C 端只向页面推 int / bool，中文字符串一律写在 .uix 内（UTF-8）。
 *
 * 入口 RunSettingsGui() 保留原签名，兼容 main.c 的托盘菜单与 --settings 路径。
 */
#include "winmover.h"
#include "ui_core.h"

/* ─── 页面与控件句柄 ───────────────────────────────── */
static UiPage   g_page = 0;
static UiWindow g_win  = 0;

static UiWidget g_wStartup, g_wManualOvr, g_wManualAct, g_wTaskbar,
                g_wFullscreen, g_wSwitcher,
                g_wPriScale, g_wSecScale,
                g_wBright, g_wOrientation,
                g_wSideL, g_wSideR,
                g_wSave, g_wRestore;

/* ─── 状态行 / 亮度：把展示态写入页面（只推 int / bool） ─── */
static void PushDisplayState(void) {
    int monCnt = GetMonitorCount();
    int bright = g_cfg.SavedBrightness;
    if (bright < 0) {
        int actual = GetSecondaryBrightness();
        if (actual >= 0) { bright = actual; g_cfg.SavedBrightness = actual; SaveConfig(); }
    }
    ui_page_set_int (g_page, "monCount",            monCnt);
    ui_page_set_bool(g_page, "manualOverride",      g_cfg.ManualOverride ? 1 : 0);
    ui_page_set_bool(g_page, "manualActive",        g_cfg.ManualActive ? 1 : 0);
    ui_page_set_int (g_page, "side",                g_cfg.SecondarySide);
    ui_page_set_bool(g_page, "brightnessSupported", bright >= 0 ? 1 : 0);
    ui_page_set_int (g_page, "brightness",          bright >= 0 ? bright : 0);
}

/* ─── 数值输入框 int <-> 宽字符 ─────────────────────── */
static void SetScaleText(UiWidget w, int v) {
    wchar_t buf[16];
    if (!w) return;
    _snwprintf(buf, 16, L"%d", v);
    ui_text_input_set_text(w, buf);
}

static int ReadScaleText(UiWidget w, int def) {
    const wchar_t* t = w ? ui_text_input_get_text(w) : NULL;
    int v;
    if (!t || !t[0]) return def;
    v = _wtoi(t);
    if (v < 50 || v > 500) return def;
    return v;
}

/* ─── 按 g_cfg 刷新控件（初始化 / 恢复默认后调用） ──── */
static void SeedWidgets(void) {
    int bright = g_cfg.SavedBrightness;

    ui_toggle_set_on(g_wStartup,    g_cfg.StartupEnabled ? 1 : 0);
    ui_toggle_set_on(g_wManualOvr,  g_cfg.ManualOverride ? 1 : 0);
    ui_toggle_set_on(g_wManualAct,  g_cfg.ManualActive ? 1 : 0);
    ui_toggle_set_on(g_wTaskbar,    g_cfg.TaskbarMoveEnabled ? 1 : 0);
    ui_toggle_set_on(g_wFullscreen, g_cfg.FullScreenMode ? 1 : 0);
    ui_toggle_set_on(g_wSwitcher,   g_cfg.SwitcherEnabled ? 1 : 0);
    ui_widget_set_enabled(g_wManualAct, g_cfg.ManualOverride ? 1 : 0);

    SetScaleText(g_wPriScale, g_cfg.PrimaryScale);
    SetScaleText(g_wSecScale, g_cfg.SecondaryScale);

    ui_slider_set_value(g_wBright, (float)(bright >= 0 ? bright : 0));
    ui_widget_set_enabled(g_wBright, bright >= 0 ? 1 : 0);

    ui_combobox_set_selected(g_wOrientation, g_cfg.SecondaryOrientation);

    PushDisplayState();
}

/* ─── 原生回调 ─────────────────────────────────────── */
/* 手动覆盖开关：实时联动"启用窗口移动"的可用性 */
static void OnManualOverride(UiWidget w, int value, void* ud) {
    int on = value ? 1 : 0;
    (void)w; (void)ud;
    if (on == g_cfg.ManualOverride) return;   /* 去抖：防绑定回环 */
    g_cfg.ManualOverride = on;
    ui_widget_set_enabled(g_wManualAct, on ? 1 : 0);
    ui_page_set_bool(g_page, "manualOverride", on);
    ui_page_set_bool(g_page, "saved", 0);
}

/* 方向下拉：立即应用 + 写回 */
static void OnOrientation(UiWidget w, int index, void* ud) {
    (void)w; (void)ud;
    if (index < 0 || index > 3 || index == g_cfg.SecondaryOrientation) return;
    g_cfg.SecondaryOrientation = index;
    if (GetMonitorCount() >= 2)
        SetOrientationAndSide(g_cfg.SecondaryOrientation, g_cfg.SecondarySide);
    SaveConfig();
    ui_page_set_bool(g_page, "saved", 0);
}

/* 左右位置分段按钮：立即应用 + 写回 */
static void OnSideClick(UiWidget w, void* ud) {
    int side = (int)(INT_PTR)ud;
    (void)w;
    g_cfg.SecondarySide = side;
    if (GetMonitorCount() >= 2)
        SetOrientationAndSide(g_cfg.SecondaryOrientation, side);
    SaveConfig();
    ui_page_set_int(g_page, "side", side);
    ui_page_set_bool(g_page, "saved", 0);
}

/* 亮度滑块：即时应用到副屏 */
static void OnBrightness(UiWidget w, float value, void* ud) {
    int v = (int)(value + 0.5f);
    (void)w; (void)ud;
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    if (v == g_cfg.SavedBrightness) return;
    g_cfg.SavedBrightness = v;
    SetSecondaryBrightness(v);
    SaveConfig();
    ui_page_set_int(g_page, "brightness", v);
    ui_page_set_bool(g_page, "saved", 0);
}

/* 保存：读回所有控件 → g_cfg → INI */
static void OnSave(UiWidget w, void* ud) {
    (void)w; (void)ud;
    g_cfg.StartupEnabled     = ui_toggle_get_on(g_wStartup) ? 1 : 0;
    g_cfg.ManualOverride     = ui_toggle_get_on(g_wManualOvr) ? 1 : 0;
    g_cfg.ManualActive       = ui_toggle_get_on(g_wManualAct) ? 1 : 0;
    g_cfg.TaskbarMoveEnabled = ui_toggle_get_on(g_wTaskbar) ? 1 : 0;
    g_cfg.FullScreenMode     = ui_toggle_get_on(g_wFullscreen) ? 1 : 0;
    g_cfg.SwitcherEnabled    = ui_toggle_get_on(g_wSwitcher) ? 1 : 0;

    g_cfg.PrimaryScale   = ReadScaleText(g_wPriScale, 150);
    g_cfg.SecondaryScale = ReadScaleText(g_wSecScale, 125);
    SetScaleText(g_wPriScale, g_cfg.PrimaryScale);
    SetScaleText(g_wSecScale, g_cfg.SecondaryScale);

    SaveConfig();
    PushDisplayState();
    ui_page_set_bool(g_page, "saved", 1);
}

/* 恢复默认：重置 g_cfg 并刷新控件（亮度不恢复，同原逻辑） */
static void OnRestore(UiWidget w, void* ud) {
    (void)w; (void)ud;
    g_cfg.PrimaryScale         = 150;
    g_cfg.SecondaryScale       = 125;
    g_cfg.SecondaryOrientation = 0;
    g_cfg.SecondarySide        = 0;
    g_cfg.FullScreenMode       = 0;
    g_cfg.TaskbarMoveEnabled   = 1;
    g_cfg.ManualOverride       = 0;
    g_cfg.ManualActive         = 1;
    g_cfg.StartupEnabled       = 1;
    g_cfg.SwitcherEnabled      = 1;
    SaveConfig();
    if (GetMonitorCount() >= 2)
        SetOrientationAndSide(g_cfg.SecondaryOrientation, g_cfg.SecondarySide);
    SeedWidgets();
    ui_page_set_bool(g_page, "saved", 0);
}

/* 窗口关闭：复位全局标记 */
static void OnWindowClose(UiWindow win, void* ud) {
    (void)win; (void)ud;
    g_guiHwnd     = NULL;
    g_guiOpenFlag = 0;
}

/* ─── 解析 / 连接控件 ──────────────────────────────── */
static void ResolveWidgets(void) {
    UiWidget root = ui_page_root(g_page);
    g_wStartup     = ui_widget_find_by_id(root, "startup");
    g_wManualOvr   = ui_widget_find_by_id(root, "manualOverride");
    g_wManualAct   = ui_widget_find_by_id(root, "manualActive");
    g_wTaskbar     = ui_widget_find_by_id(root, "taskbar");
    g_wFullscreen  = ui_widget_find_by_id(root, "fullscreen");
    g_wSwitcher    = ui_widget_find_by_id(root, "switcher");
    g_wPriScale    = ui_widget_find_by_id(root, "primaryScale");
    g_wSecScale    = ui_widget_find_by_id(root, "secondaryScale");
    g_wBright      = ui_widget_find_by_id(root, "brightness");
    g_wOrientation = ui_widget_find_by_id(root, "orientation");
    g_wSideL       = ui_widget_find_by_id(root, "sideLeft");
    g_wSideR       = ui_widget_find_by_id(root, "sideRight");
    g_wSave        = ui_widget_find_by_id(root, "save");
    g_wRestore     = ui_widget_find_by_id(root, "restore");
}

static void WireWidgets(void) {
    ui_toggle_on_changed(g_wManualOvr, OnManualOverride, NULL);

    ui_widget_on_click(g_wSideL, OnSideClick, (void*)(INT_PTR)0);
    ui_widget_on_click(g_wSideR, OnSideClick, (void*)(INT_PTR)1);

    ui_slider_on_changed(g_wBright, OnBrightness, NULL);
    ui_combobox_on_changed(g_wOrientation, OnOrientation, NULL);

    ui_widget_on_click(g_wSave, OnSave, NULL);
    ui_widget_on_click(g_wRestore, OnRestore, NULL);
}

/* ─── 设置界面入口 ─────────────────────────────────── */
int RunSettingsGui(HINSTANCE hInst, int nCmdShow) {
    HANDLE hMutex;
    wchar_t wdir[MAX_PATH], path[MAX_PATH];

    (void)hInst; (void)nCmdShow;

    /* 单实例：已有设置窗口则激活后返回（core-ui 无固定类名，按唯一标题查找） */
    hMutex = CreateMutexA(NULL, FALSE, "WindowMoveSettingsMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hW = FindWindowW(NULL, L"Secondary Display Assistant \u2014 \u8bbe\u7f6e");
        if (hW) { ShowWindow(hW, SW_SHOW); SetForegroundWindow(hW); }
        CloseHandle(hMutex);
        return 0;
    }

    LoadConfig();

    ui_init_with_theme(UI_THEME_DARK);

    /* 载入 settings.uix（exe 同目录） */
    MultiByteToWideChar(CP_ACP, 0, g_exeDir, -1, wdir, MAX_PATH);
    _snwprintf(path, MAX_PATH, L"%s\\settings.uix", wdir);
    g_page = ui_page_load_file(path);
    if (!g_page) {
        MessageBoxW(NULL, L"\u65e0\u6cd5\u52a0\u8f7d\u8bbe\u7f6e\u754c\u9762 settings.uix",
                    L"Secondary Display Assistant", MB_ICONERROR);
        ui_shutdown();
        CloseHandle(hMutex);
        return 1;
    }

    g_win = ui_page_open_window(g_page, NULL);
    if (!g_win) {
        ui_page_destroy(g_page);
        g_page = 0;
        ui_shutdown();
        CloseHandle(hMutex);
        return 1;
    }
    g_guiHwnd = (HWND)ui_window_hwnd(g_win);

    ResolveWidgets();
    if (!g_wStartup || !g_wManualOvr || !g_wManualAct || !g_wTaskbar ||
        !g_wFullscreen || !g_wSwitcher || !g_wPriScale || !g_wSecScale ||
        !g_wBright || !g_wOrientation || !g_wSideL || !g_wSideR ||
        !g_wSave || !g_wRestore) {
        MessageBoxW(NULL, L"\u8bbe\u7f6e\u754c\u9762\u63a7\u4ef6\u52a0\u8f7d\u5931\u8d25\uff08settings.uix \u4e0e\u7a0b\u5e8f\u7248\u672c\u4e0d\u5339\u914d\uff09",
                    L"Secondary Display Assistant", MB_ICONERROR);
        ui_page_destroy(g_page);
        g_page = 0;
        ui_shutdown();
        CloseHandle(hMutex);
        return 1;
    }

    SeedWidgets();          /* 先灌初值，再接回调，避免初始化触发回环 */
    WireWidgets();
    ui_window_on_close(g_win, OnWindowClose, NULL);

    ui_run();

    g_guiHwnd     = NULL;
    g_guiOpenFlag = 0;
    if (g_page) ui_page_destroy(g_page);
    g_page = 0;
    g_win  = 0;
    ui_shutdown();
    CloseHandle(hMutex);
    return 0;
}
