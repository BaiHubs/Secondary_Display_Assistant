/* config.c — INI 配置读写
 *
 * 负责 WindowMove.ini 的读写，并把配置应用为运行时状态：
 *   - IniReadInt() / IniWriteInt()  单个键值读写
 *   - LoadConfig() / SaveConfig()   整体加载 / 保存
 *   - ApplyConfigFromIni()          重新读取并应用（含开机自启注册表）
 *   - CheckIniChanged()             监听 INI 变更（由定时器周期调用）
 */
#include "winmover.h"

char g_lastIniTime[32] = "";

void GetIniPath(void) {
    _snprintf(g_iniPath, MAX_PATH, "%s\\WindowMove.ini", g_exeDir);
}

int IniReadInt(const char *sec, const char *key, int def) {
    char buf[32] = {0};
    GetPrivateProfileStringA(sec, key, "", buf, sizeof(buf), g_iniPath);
    return buf[0] ? atoi(buf) : def;
}

void IniWriteInt(const char *sec, const char *key, int val) {
    char buf[32];
    _snprintf(buf, sizeof(buf), "%d", val);
    WritePrivateProfileStringA(sec, key, buf, g_iniPath);
}

void LoadConfig(void) {
    g_cfg.PrimaryScale         = IniReadInt("Settings", "PrimaryScale", 150);
    g_cfg.SecondaryScale       = IniReadInt("Settings", "SecondaryScale", 125);
    g_cfg.ManualOverride       = IniReadInt("Settings", "ManualOverride", 0);
    g_cfg.ManualActive         = IniReadInt("Settings", "ManualActive", 1);
    g_cfg.FullScreenMode       = IniReadInt("Settings", "FullScreenMode", 0);
    g_cfg.TaskbarMoveEnabled   = IniReadInt("Settings", "TaskbarMoveEnabled", 1);
    g_cfg.SavedBrightness      = IniReadInt("Settings", "SecondaryBrightness", -1);
    g_cfg.SecondaryOrientation = IniReadInt("Settings", "SecondaryOrientation", 0);
    g_cfg.SecondarySide        = IniReadInt("Settings", "SecondarySide", 0);
    g_cfg.StartupEnabled       = IniReadInt("Settings", "StartupEnabled", 1);
    g_cfg.SwitcherEnabled      = IniReadInt("Settings", "SwitcherEnabled", 1);
}

void SaveConfig(void) {
    IniWriteInt("Settings", "PrimaryScale",         g_cfg.PrimaryScale);
    IniWriteInt("Settings", "SecondaryScale",       g_cfg.SecondaryScale);
    IniWriteInt("Settings", "ManualOverride",       g_cfg.ManualOverride);
    IniWriteInt("Settings", "ManualActive",         g_cfg.ManualActive);
    IniWriteInt("Settings", "FullScreenMode",       g_cfg.FullScreenMode);
    IniWriteInt("Settings", "TaskbarMoveEnabled",   g_cfg.TaskbarMoveEnabled);
    IniWriteInt("Settings", "SecondaryBrightness",  g_cfg.SavedBrightness);
    IniWriteInt("Settings", "SecondaryOrientation", g_cfg.SecondaryOrientation);
    IniWriteInt("Settings", "SecondarySide",        g_cfg.SecondarySide);
    IniWriteInt("Settings", "StartupEnabled",       g_cfg.StartupEnabled);
    IniWriteInt("Settings", "SwitcherEnabled",      g_cfg.SwitcherEnabled);
}

void ApplyConfigFromIni(void) {
    Config prev = g_cfg;

    /* 从 INI 重新读取 */
    g_cfg.FullScreenMode       = IniReadInt("Settings", "FullScreenMode", 0);
    g_cfg.TaskbarMoveEnabled   = IniReadInt("Settings", "TaskbarMoveEnabled", 1);
    g_cfg.PrimaryScale         = IniReadInt("Settings", "PrimaryScale", 150);
    g_cfg.SecondaryScale       = IniReadInt("Settings", "SecondaryScale", 125);
    g_cfg.SecondaryOrientation = IniReadInt("Settings", "SecondaryOrientation", 0);
    g_cfg.SecondarySide        = IniReadInt("Settings", "SecondarySide", 0);
    g_cfg.SavedBrightness      = IniReadInt("Settings", "SecondaryBrightness", -1);
    g_cfg.ManualOverride       = IniReadInt("Settings", "ManualOverride", 0);
    g_cfg.ManualActive         = IniReadInt("Settings", "ManualActive", 1);
    g_cfg.StartupEnabled       = IniReadInt("Settings", "StartupEnabled", 1);
    g_cfg.SwitcherEnabled      = IniReadInt("Settings", "SwitcherEnabled", 1);

    g_fullScreenMode       = g_cfg.FullScreenMode;
    g_taskbarMoveEnabled   = g_cfg.TaskbarMoveEnabled;
    g_primaryScale         = g_cfg.PrimaryScale;
    g_secondaryScale       = g_cfg.SecondaryScale;

    UpdateActiveState();

    if (GetMonitorCount() >= 2) {
        if (g_cfg.SavedBrightness >= 0)
            SetSecondaryBrightness(g_cfg.SavedBrightness);
        SetOrientationAndSide(g_cfg.SecondaryOrientation, g_cfg.SecondarySide);
    }

    /* 处理开机自启 — 用 HKCU\\Run 注册表（无需管理员，静默） */
    {
        HKEY hKey = NULL;
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
                          "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                          0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            if (g_cfg.StartupEnabled) {
                char path[MAX_PATH];
                _snprintf(path, sizeof(path), "%s\\Secondary Display Assistant.exe", g_exeDir);
                RegSetValueExA(hKey, "WindowMoveHelper", 0, REG_SZ,
                               (const BYTE*)path, (DWORD)(strlen(path) + 1));
            } else {
                RegDeleteValueA(hKey, "WindowMoveHelper");
            }
            RegCloseKey(hKey);
        }
        IniWriteInt("Settings", "_TaskExists", g_cfg.StartupEnabled ? 1 : 0);
    }
}

void CheckIniChanged(void) {
    char buf[32];
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(g_iniPath, GetFileExInfoStandard, &info))
        return;
    _snprintf(buf, sizeof(buf), "%u", info.ftLastWriteTime.dwLowDateTime);
    if (strcmp(buf, g_lastIniTime) != 0) {
        strcpy(g_lastIniTime, buf);
        ApplyConfigFromIni();
    }
}
