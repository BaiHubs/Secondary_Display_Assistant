/* winmover.h — 公共头文件（窗口移动助手 Secondary Display Assistant）
 *
 * 集中声明：配置结构体 Config、跨模块全局变量（extern）、
 * 窗口/定时器等常量，以及各模块的公开函数原型。
 * 实现分散于 src/ 下各 .c 文件（见每个函数上方模块注释）。
 */
#ifndef WINMOVER_H
#define WINMOVER_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
WINUSERAPI void WINAPI SwitchToThisWindow(HWND, BOOL);
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

/* ─── 配置结构体 ──────────────────────────────────── */
typedef struct {
    int PrimaryScale;
    int SecondaryScale;
    int ManualOverride;
    int ManualActive;
    int FullScreenMode;
    int TaskbarMoveEnabled;
    int SavedBrightness;
    int SecondaryOrientation;
    int SecondarySide;
    int StartupEnabled;
    int SwitcherEnabled;
} Config;

/* ─── 全局状态 ────────────────────────────────────── */
extern Config   g_cfg;
extern char     g_exeDir[MAX_PATH];
extern char     g_iniPath[MAX_PATH];
extern int      g_isActive;
extern int      g_movingFlag;
extern int      g_resizingFlag;
extern int      g_taskbarMoveEnabled;
extern int      g_fullScreenMode;
extern int      g_primaryScale;
extern int      g_secondaryScale;
extern HWND     g_guiHwnd;
extern int      g_guiOpenFlag;
extern HINSTANCE g_hInst;
extern HWND     g_hMainWnd;

/* ─── 常量 ────────────────────────────────────────── */
#define WM_TRAY_ICON      (WM_USER + 100)
#define WM_GUI_CLOSED     (WM_USER + 101)
#define ID_TRAY_OPEN      1001
#define ID_TRAY_RELOAD    1002
#define ID_TRAY_EXIT      1003
#define IDT_INI_CHECK     1004
#define IDT_STATUS        1005

#define SIDE_LEFT   0
#define SIDE_RIGHT  1

/* ─── 函数声明（按模块分节） ──────────────────────── */

/* main.c — 主入口与全局状态 */
void  UpdateActiveState(void);
void  OpenSettingsGui(void);
int   ResolveResPath(const char *name, wchar_t *out, int cch);

/* wndproc.c — 主窗口消息过程 */
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* config.c — INI 配置读写 */
void  GetIniPath(void);
void  LoadConfig(void);
void  SaveConfig(void);
int   IniReadInt(const char *sec, const char *key, int def);
void  IniWriteInt(const char *sec, const char *key, int val);
void  ApplyConfigFromIni(void);
void  CheckIniChanged(void);

/* monitor.c — 显示器查询 */
int   GetMonitorCount(void);
int   IsPrimaryMonitor(HMONITOR hMon);
HMONITOR GetPrimaryMonitorHandle(void);
HMONITOR GetMonitorFromWindowEx(HWND hWnd);
HMONITOR GetMonitorFromPointEx(int x, int y);
int   GetMonitorRect(HMONITOR hMon, RECT *rc);
int   GetMonitorWorkRect(HMONITOR hMon, RECT *rc);
int   GetMonitorScale(HMONITOR hMon);

/* move.c — 窗口移动 */
void  MoveWindowToMonitor(HWND hWnd, HMONITOR hTarget, int fullScreen);
void  DelayedMove(HWND hWnd, HMONITOR hMonMouse);

/* window_filter.c — 窗口筛选 / 分类 */
int   IsExcludedWindow(HWND hWnd);
int   IsRealAppWindow(HWND hWnd);

/* shellhook.c — Shell 钩子 */
void  ShellHookInit(HWND hWnd);
int   ShellHookHandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
HWND  ResolveMoveTarget(HWND hWnd);  /* 把 shell 钩子窗口规范化为可移动的根顶层窗口 */

/* hotkeys.c — 全局热键 */
void  RegisterAppHotKeys(HWND hWnd);
void  HandleHotKey(int id);

/* switcher.c — 窗口切换器 */
int   SwitcherHandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
void  SwitcherStartFromHotkey(void);
int   RunSwitcherProcess(HINSTANCE hInst, int nCmdShow);

/* tray.c — 托盘图标与菜单 */
void  CreateTrayIcon(HWND hWnd);
void  DestroyTrayIcon(HWND hWnd);
void  TrayHandleMessage(HWND hWnd, WPARAM wParam, LPARAM lParam);

/* brightness.c — DDC/CI 亮度 */
int   GetSecondaryBrightness(void);
void  SetSecondaryBrightness(int val);

/* display_cmd.c — 副屏方向与位置 */
int   GetCurrentOrientation(void);
int   SetOrientationAndSide(int orientation, int side);

/* settings.c — 设置界面 */
int   RunSettingsGui(HINSTANCE hInst, int nCmdShow);

#endif /* WINMOVER_H */
