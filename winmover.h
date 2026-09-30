/* winmover.h — 窗口移动助手 C 版 */
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

/* ─── 函数声明 ────────────────────────────────────── */
/* config.c */
void  GetIniPath(void);
void  LoadConfig(void);
void  SaveConfig(void);
int   IniReadInt(const char *sec, const char *key, int def);
void  IniWriteInt(const char *sec, const char *key, int val);
void  ApplyConfigFromIni(void);
void  CheckIniChanged(void);
void UpdateActiveState(void);

/* monitor.c */
int   GetMonitorCount(void);
int   IsPrimaryMonitor(HMONITOR hMon);
HMONITOR GetPrimaryMonitorHandle(void);
HMONITOR GetMonitorFromWindowEx(HWND hWnd);
HMONITOR GetMonitorFromPointEx(int x, int y);
int   GetMonitorRect(HMONITOR hMon, RECT *rc);
int   GetMonitorWorkRect(HMONITOR hMon, RECT *rc);
int   GetMonitorScale(HMONITOR hMon);

/* move.c */
void  MoveWindowToMonitor(HWND hWnd, HMONITOR hTarget, int fullScreen);
int   IsExcludedWindow(HWND hWnd);
void  DelayedMove(HWND hWnd, HMONITOR hMonMouse);

/* brightness.c */
int   GetSecondaryBrightness(void);
void  SetSecondaryBrightness(int val);

/* display_cmd.c */
int   GetCurrentOrientation(void);
int   SetOrientationAndSide(int orientation, int side);

/* gui.c */
void  CreateTrayIcon(HWND hWnd);
void  DestroyTrayIcon(HWND hWnd);

/* winmover.c */
void  ShowMonitorSwitcher(void);

/* settings.c */
int   RunSettingsGui(HINSTANCE hInst, int nCmdShow);
void  OpenSettingsGui(void);

#endif /* WINMOVER_H */
