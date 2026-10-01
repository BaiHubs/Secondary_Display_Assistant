/* main.c — 程序主入口与全局状态
 *
 * 本文件负责“生命周期与全局状态”这一职责：
 *   - 定义所有跨模块共享的全局变量（声明见 winmover.h）；
 *   - 解析可执行文件所在目录（g_exeDir）；
 *   - 根据配置/显示器数量刷新激活状态 UpdateActiveState()；
 *   - 设置界面入口 OpenSettingsGui()（单进程复用）；
 *   - WinMain()：单实例互斥、运行参数解析、注册窗口类、
 *     创建隐藏主窗口、安装托盘图标并进入消息循环。
 *
 * 其余功能分别位于：
 *   wndproc.c       主窗口消息分发
 *   shellhook.c     Shell 钩子（窗口创建/激活/销毁）
 *   hotkeys.c       全局热键
 *   switcher.c      窗口切换器
 *   window_filter.c 窗口筛选/分类
 *   tray.c          托盘图标与菜单
 *   config/monitor/move/brightness/display_cmd/settings 等各司其职
 */
#include "winmover.h"

/* ─── 全局状态定义（对应 winmover.h 中的 extern 声明） ─── */
Config  g_cfg;
char    g_exeDir[MAX_PATH];
char    g_iniPath[MAX_PATH];
int     g_isActive        = 0;
int     g_movingFlag      = 0;
int     g_resizingFlag    = 0;
int     g_taskbarMoveEnabled = 1;
int     g_fullScreenMode  = 0;
int     g_primaryScale    = 150;
int     g_secondaryScale  = 125;
HWND    g_guiHwnd         = NULL;
int     g_guiOpenFlag     = 0;
HINSTANCE g_hInst;
HWND    g_hMainWnd;

/* ─── 取可执行文件所在目录，写入 g_exeDir ─────────────── */
static void GetExeDir(void) {
    GetModuleFileNameA(NULL, g_exeDir, MAX_PATH);
    char *p = strrchr(g_exeDir, '\\');
    if (p) *p = '\0';
}

/* ─── 依据配置/显示器数量刷新 g_isActive ─────────────── */
void UpdateActiveState(void) {
    g_isActive = g_cfg.ManualOverride
                 ? g_cfg.ManualActive
                 : (GetMonitorCount() >= 2);
}

/* ─── 设置界面入口（单进程复用已开窗口） ─────────────── */
void OpenSettingsGui(void) {
    /* 以独立进程启动设置界面：
       - core-ui 需要"干净的"进程来设置 Per-Monitor DPI V2（进程 DPI 感知只能设一次，
         主进程已 SetProcessDPIAware，若在进程内跑 core-ui 会被降级为 System 感知）；
       - 也避免与托盘的消息循环 / INI 轮询定时器（含 DDC/CI）共用 UI 线程。
       单实例由子进程内部的互斥体 + 窗口查找保证。 */
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    ShellExecuteA(NULL, "open", exe, "--settings", NULL, SW_SHOWNORMAL);
}

/* ─── WinMain：程序入口 ──────────────────────────────── */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nCmdShow) {
    (void)hPrev;

    g_hInst = hInst;
    GetExeDir();
    GetIniPath();

    /* --settings 参数 → 直接运行设置界面 */
    if (lpCmd && lpCmd[0] && strstr(lpCmd, "--settings")) {
        /* 刻意不在此调用 SetProcessDPIAware()：进程 DPI 感知只能设置一次，
           留给 core-ui 自己设置 Per-Monitor V2，否则会被降级为 System 感知
           导致渲染缩放错误、拖动发虚/卡顿。 */
        return RunSettingsGui(hInst, nCmdShow);
    }

    SetProcessDPIAware();

    /* 单实例：已有实例则激活后退出 */
    HANDLE hMutex = CreateMutexA(NULL, FALSE, "WindowMoveMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hW = FindWindowA("WindowMoveClass", NULL);
        if (hW) { ShowWindow(hW, SW_SHOW); SetForegroundWindow(hW); }
        CloseHandle(hMutex);
        return 0;
    }

    LoadConfig();
    UpdateActiveState();

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    /* 注册主窗口类并创建隐藏窗口（仅用于接收消息） */
    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIconA(hInst, MAKEINTRESOURCEA(1));
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "WindowMoveClass";
    RegisterClassA(&wc);

    g_hMainWnd = CreateWindowA("WindowMoveClass", "Secondary Display Assistant",
                               0, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
    if (!g_hMainWnd) { CloseHandle(hMutex); return 1; }

    CreateTrayIcon(g_hMainWnd);

    /* 启动时按配置应用副屏亮度与方向 */
    if (GetMonitorCount() >= 2) {
        if (g_cfg.SavedBrightness >= 0)
            SetSecondaryBrightness(g_cfg.SavedBrightness);
        SetOrientationAndSide(g_cfg.SecondaryOrientation, g_cfg.SecondarySide);
    }

    /* 主消息循环 */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CloseHandle(hMutex);
    return 0;
}
