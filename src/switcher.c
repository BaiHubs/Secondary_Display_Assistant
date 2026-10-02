/* switcher.c — Alt+Tab 仿原生切换器（DWM 实时缩略图 + Core UI .uix 界面）
 *
 * 枚举可见应用窗口 → 按所在显示器分组 → 用 switcher.uix 渲染外壳
 * （分组标签 / 缩略图占位 / 标题 / 选中高亮 / 操作提示）→ 在槽位上注册 DWM
 * 实时缩略图。旧版文本列表切换器（ShowMonitorSwitcher）已删除。
 *
 * 布局：**每组独立计算列数**（副屏只有 1 个窗口 → 1 列，主屏 2 个 → 2 列），
 * 每组卡片宽度由本组列数决定，卡片之间纵向堆叠并以「主屏 / 副屏」标签划界。
 * 每组的 cardWidth / cardHeight 通过 groups JSON 发给 .uix，卡片用 :width /
 * :height 明确绑定，这样卡片宽度和高度不再依赖 flex 的 intrinsic size 计算。
 * 窗口尺寸取**最大组**的卡片宽度 + 统一左右外边距，缩略图 rcDestination 由与
 * .uix CSS 一致的算术布局换算，并按**目标显示器** DPI 缩放。
 *
 * 对外接口（供 hotkeys.c / wndproc.c 使用）：
 *   SwitcherStartFromHotkey()  热键唤起
 *   SwitcherHandleMessage()    兼容保留（当前无自定义消息）
 *
 * 依赖：Core UI（ui_core.h）+ shcore；主进程 DPI 感知须为 Per-Monitor V2
 *       （见 main.c），core-ui / GetDpiForMonitor 才能取到监视器真实 DPI。
 */
#include "winmover.h"
#include "ui_core.h"
#include <shellscalingapi.h>

/* ─── 布局常量（须与 switcher.uix 的样式保持一致） ─── */
#define PAD_X 28                                            /* .groups 左/右内边距 */
#define PAD_TOP 24                                          /* .groups 上内边距 */
#define GROUP_GAP 16                                        /* .groups gap（组间距） */
#define BOX_PAD 14                                          /* .group 内边距（分组卡片） */
#define LABEL_H 18                                          /* .gtag 高 */
#define LABEL_GAP 8                                         /* .group gap（标签与网格间距） */
#define GTH (BOX_PAD + LABEL_H + LABEL_GAP)                 /* 卡片顶 → 网格 */
#define BOX_EXTRA (BOX_PAD + LABEL_H + LABEL_GAP + BOX_PAD) /* 卡片高 = gridH + BOX_EXTRA */
#define CELL_W 300                                          /* .cell 宽 */
#define CELL_H 188                                          /* .cell 高 */
#define GAP 20                                              /* .grid gap（行/列间距） */
#define CAP_GAP 8                                           /* .slot gap */
#define CAP_H 20                                            /* .cap 高 */
#define SLOT_H (CELL_H)                                     /* 槽位高 = 单元高（标题在单元内底部） */
#define HINT_H 52                                           /* 提示行（上14 + 文18 + 下20） */
#define MAX_COLS 4                                          /* 每排最多几个，超出即换排（两排 / 多排） */

#define MAX_GROUPS 8
#define MAX_PER_GRP 12
#define MAX_TOTAL (MAX_GROUPS * MAX_PER_GRP)
#define SW_JSON_MAX 32768

typedef struct
{
    HWND hwnd;
    char title[768];
} SwWin;
typedef struct
{
    HMONITOR mon;
    int isPrim;
    int n;
    int cols; /* 本组列数（每组独立算） */
    SwWin w[MAX_PER_GRP];
} SwGroup;

static SwGroup g_grp[MAX_GROUPS];
static int g_grpCount = 0;
static HWND g_targets[MAX_TOTAL]; /* 渲染顺序 → 窗口句柄 */
static RECT g_cellHit[MAX_TOTAL]; /* 渲染顺序 → 槽位命中矩形(DIP) */
static HTHUMBNAIL g_thumbs[MAX_TOTAL];
static int g_swTotal = 0;

static int g_swActive = 0;
static int g_swInited = 0;
static UiPage g_swPage = 0;
static UiWindow g_swWin = 0;
static HWND g_swHwnd = NULL;
static float g_swDpi = 1.0f;
static HMONITOR g_swMon = NULL;
static POINT g_swHookCursor = {0, 0};
static int g_swDone = 0;
static int g_swConfirm = 0;
static HWND g_swPick = NULL;
static int g_swShown = 0;
static WNDPROC g_swOrigProc = NULL;

static void AltTabShow(void);

/* ─── ANSI → UTF-8 ─────────────────────────────────── */
static void AnsiToUtf8(const char *src, char *out, int cap)
{
    wchar_t wbuf[512];
    int n = MultiByteToWideChar(CP_ACP, 0, src, -1, wbuf, 512);
    if (n <= 0)
    {
        if (cap > 0)
            out[0] = 0;
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, out, cap, NULL, NULL);
    out[cap - 1] = 0;
}

/* ─── JSON 字符串转义（追加写入 dst） ───────────────── */
static void JsonAppendEscaped(char *dst, int cap, const char *s)
{
    int len = (int)strlen(dst);
    while (*s && len < cap - 2)
    {
        char c = *s++;
        if (c == '"' || c == '\\')
        {
            dst[len++] = '\\';
            dst[len++] = c;
        }
        else if ((unsigned char)c < 0x20)
        { /* 丢弃控制字符 */
        }
        else
            dst[len++] = c;
    }
    dst[len] = 0;
}

/* ─── 目标显示器 DPI 缩放（非感知进程会返回 96，故主进程须 PMv2） ─── */
static float MonScale(HMONITOR mon)
{
    UINT dx = 96, dy = 96;
    if (mon && SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy)) && dx > 0)
        return (float)dx / 96.0f;
    return 1.0f;
}

/* ─── 读取页面当前 sel ─────────────────────────────── */
static int ReadSel(void)
{
    char *j = ui_page_get_json(g_swPage, "sel");
    int s = (j ? atoi(j) : 0);
    if (j)
        ui_page_free(j);
    return s;
}

/* ─── 结束切换器：confirm=1 激活第 idx 个窗口，否则取消 ─── */
static void SwDone(int confirm, int idx)
{
    g_swDone = 1;
    g_swConfirm = confirm;
    g_swPick = (confirm && idx >= 0 && idx < g_swTotal) ? g_targets[idx] : NULL;
    ReleaseCapture(); /* 释放鼠标捕获：否则隐藏(不销毁)后仍被本进程占有 → 光标闪烁 */
    ui_quit(0);
}

/* ─── 键盘：方向循环 / Enter 确认 / Esc 取消 ─────────── */
static void OnKey(UiWindow win, int vk, void *ud)
{
    (void)win;
    (void)ud;
    if (g_swDone)
        return;
    if (vk == VK_LEFT || vk == VK_UP || vk == VK_RIGHT || vk == VK_DOWN)
    {
        int dir = (vk == VK_LEFT || vk == VK_UP) ? -1 : 1;
        int s = ReadSel();
        if (g_swTotal > 0)
            s = (s + dir + g_swTotal) % g_swTotal;
        ui_page_set_int(g_swPage, "sel", s);
    }
    else if (vk == VK_RETURN || vk == VK_SPACE)
    {
        SwDone(1, ReadSel());
    }
    else if (vk == VK_ESCAPE)
    {
        SwDone(0, -1);
    }
}

/* ─── 窗口子类化：点击窗口外即关闭（鼠标捕获方案）。
 *   · WA_ACTIVE：SetCapture —— 窗口激活后，窗口外任意位置的鼠标按下都会
 *     作为 WM_LBUTTONDOWN 送到本窗口，从而"点窗口外任意处一点即关闭"；
 *   · WM_LBUTTONDOWN：坐标在客户区外 → 关闭；客户区内 → 命中缩略图则激活；
 *   · WA_INACTIVE：释放捕获（失去前台后不再拦截别处点击）。 ─── */
static LRESULT CALLBACK SwSubclassProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (!g_swDone && g_swShown)
    {
        if (msg == WM_ACTIVATE)
        {
            if (LOWORD(wp) == WA_ACTIVE)
                SetCapture(hWnd);
            else if (LOWORD(wp) == WA_INACTIVE)
                ReleaseCapture();
        }
        else if (msg == WM_LBUTTONDOWN)
        {
            int mx = (int)(short)LOWORD(lp), my = (int)(short)HIWORD(lp);
            RECT rc;
            GetClientRect(hWnd, &rc);
            if (mx < rc.left || my < rc.top || mx >= rc.right || my >= rc.bottom)
            {
                /* 点击点落在窗口之外 → 关闭 */
                ReleaseCapture();
                SwDone(0, -1);
            }
            else
            {
                /* 客户区物理像素 → DIP，命中缩略图则激活 */
                int cx = (int)(mx / g_swDpi), cy = (int)(my / g_swDpi);
                int i;
                for (i = 0; i < g_swTotal; i++)
                {
                    RECT *r = &g_cellHit[i];
                    if (cx >= r->left && cx < r->right && cy >= r->top && cy < r->bottom)
                    {
                        ReleaseCapture();
                        SwDone(1, i); /* 点中缩略图 → 激活对应窗口 */
                        break;
                    }
                }
            }
        }
    }
    return CallWindowProcW(g_swOrigProc, hWnd, msg, wp, lp);
}

/* ─── 枚举窗口 → 按所在显示器分组（主屏组在前，其余按屏幕左边界排序） ─── */
static void CollectGroups(void)
{
    HWND hWnd = GetTopWindow(NULL);
    int i, j;
    g_grpCount = 0;
    while (hWnd)
    {
        if (IsWindowVisible(hWnd) && IsRealAppWindow(hWnd))
        {
            char titleA[256];
            if (GetWindowTextA(hWnd, titleA, sizeof(titleA)) && titleA[0])
            {
                HMONITOR hm = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
                int gi = -1;
                for (i = 0; i < g_grpCount; i++)
                    if (g_grp[i].mon == hm)
                    {
                        gi = i;
                        break;
                    }
                if (gi < 0)
                {
                    if (g_grpCount >= MAX_GROUPS)
                        goto next;
                    gi = g_grpCount++;
                    g_grp[gi].mon = hm;
                    g_grp[gi].isPrim = IsPrimaryMonitor(hm);
                    g_grp[gi].n = 0;
                    g_grp[gi].cols = 1;
                }
                if (g_grp[gi].n < MAX_PER_GRP)
                {
                    SwWin *w = &g_grp[gi].w[g_grp[gi].n++];
                    w->hwnd = hWnd;
                    AnsiToUtf8(titleA, w->title, sizeof(w->title));
                }
            }
        }
    next:
        hWnd = GetNextWindow(hWnd, GW_HWNDNEXT);
    }
    /* 组排序：主屏优先，其次按屏幕左边界 */
    for (i = 1; i < g_grpCount; i++)
    {
        SwGroup key = g_grp[i];
        RECT kr;
        GetMonitorRect(key.mon, &kr);
        j = i - 1;
        while (j >= 0)
        {
            RECT jr;
            GetMonitorRect(g_grp[j].mon, &jr);
            int keepBefore = (g_grp[j].isPrim && !key.isPrim) ||
                             (g_grp[j].isPrim == key.isPrim && jr.left <= kr.left);
            if (keepBefore)
                break;
            g_grp[j + 1] = g_grp[j];
            j--;
        }
        g_grp[j + 1] = key;
    }
}

/* ─── 组装 groups JSON（Chinese 标签留给 .uix，C 只发 isPrimary）
 *   每组额外发 cardWidth / cardHeight，让 .uix 用 :width / :height 明确绑定，
 *   避免依赖 flex 的 intrinsic size 计算导致卡片塌陷 / 被 stretch。 ─── */
static void BuildJson(char *json)
{
    int gi, k, len = 0, gidx = 0;
    json[len++] = '[';
    json[len] = 0;
    for (gi = 0; gi < g_grpCount; gi++)
    {
        char gbuf[160];
        int cols = g_grp[gi].cols;
        int n = g_grp[gi].n;
        int rows = (n + cols - 1) / cols;
        int cw = 2 * BOX_PAD + cols * CELL_W + (cols - 1) * GAP;
        int ch = BOX_EXTRA + rows * CELL_H + (rows - 1) * GAP;
        _snprintf(gbuf, sizeof(gbuf),
                  "%s{\"isPrimary\":%s,\"cardWidth\":%d,\"cardHeight\":%d,\"items\":[",
                  gi ? "," : "", g_grp[gi].isPrim ? "true" : "false", cw, ch);
        strcat(json, gbuf);
        len += (int)strlen(gbuf);
        for (k = 0; k < n; k++)
        {
            char item[1200];
            g_targets[gidx] = g_grp[gi].w[k].hwnd;
            _snprintf(item, sizeof(item), "%s{\"id\":%d,\"gidx\":%d,\"title\":\"",
                      k ? "," : "", gidx, gidx);
            JsonAppendEscaped(item, sizeof(item), g_grp[gi].w[k].title);
            strcat(item, "\"}");
            if (len + (int)strlen(item) >= SW_JSON_MAX - 2)
                break;
            strcat(json, item);
            len += (int)strlen(item);
            gidx++;
        }
        strcat(json, "]}");
        len += 2;
    }
    strcat(json, "]");
    g_swTotal = gidx;
}

/* ─── 注册 DWM 缩略图：每组用**本组**列数算位置。
 *   缩略图按**源窗口自身宽高比**等比缩小、居中放在槽位里（留黑边），
 *   而不是拉伸填满整个槽位 —— 看起来就是"那个窗口被缩小了"。 ─── */
static void RegisterThumbs(void)
{
    int gi, k, gidx = 0;
    int y = PAD_TOP; /* 当前组卡片顶部（.groups 内容坐标，DIP） */
    for (gi = 0; gi < g_grpCount; gi++)
    {
        int n = g_grp[gi].n;
        int columns = g_grp[gi].cols; /* 本组列数 */
        int rows = (n + columns - 1) / columns;
        int gridTop = y + GTH;
        for (k = 0; k < n; k++)
        {
            int row = k / columns, col = k % columns;
            float cxd = (float)(PAD_X + BOX_PAD + col * (CELL_W + GAP)); /* 槽位左上(DIP) */
            float cyd = (float)(gridTop + row * (SLOT_H + GAP));
            float cellL = cxd * g_swDpi, cellT = cyd * g_swDpi; /* 槽位左上(物理) */
            float cellW = (float)CELL_W * g_swDpi;
            float cellH = (float)(CELL_H - CAP_H) * g_swDpi; /* 底部预留标题条 */

            /* 命中矩形（DIP，整槽含标题，用于点击判定） */
            g_cellHit[gidx].left = (LONG)cxd;
            g_cellHit[gidx].top = (LONG)cyd;
            g_cellHit[gidx].right = (LONG)(cxd + CELL_W);
            g_cellHit[gidx].bottom = (LONG)(cyd + SLOT_H);

            g_thumbs[gidx] = NULL;
            if (g_targets[gidx] &&
                DwmRegisterThumbnail(g_swHwnd, g_targets[gidx], &g_thumbs[gidx]) == S_OK && g_thumbs[gidx])
            {
                SIZE src = {0, 0};
                int sw = 0, sh = 0;
                float sc, tw, th, offx, offy;
                RECT r;
                DWM_THUMBNAIL_PROPERTIES dtp;

                DwmQueryThumbnailSourceSize(g_thumbs[gidx], &src);
                sw = src.cx;
                sh = src.cy;
                if (sw <= 0 || sh <= 0) /* 兜底：用源窗口矩形 */
                {
                    RECT wr;
                    if (GetWindowRect(g_targets[gidx], &wr))
                    {
                        sw = wr.right - wr.left;
                        sh = wr.bottom - wr.top;
                    }
                }
                if (sw <= 0 || sh <= 0)
                {
                    sw = (int)cellW;
                    sh = (int)cellH;
                }

                /* 等比缩放到能放进槽位，居中 */
                sc = cellW / (float)sw;
                if (cellH / (float)sh < sc)
                    sc = cellH / (float)sh;
                tw = sw * sc;
                th = sh * sc;
                offx = (cellW - tw) / 2.0f;
                offy = (cellH - th) / 2.0f;

                r.left = (LONG)(cellL + offx);
                r.top = (LONG)(cellT + offy);
                r.right = (LONG)(cellL + offx + tw);
                r.bottom = (LONG)(cellT + offy + th);

                ZeroMemory(&dtp, sizeof(dtp));
                dtp.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_RECTDESTINATION | DWM_TNP_OPACITY;
                dtp.fVisible = TRUE;
                dtp.opacity = 255;
                dtp.rcDestination = r;
                DwmUpdateThumbnailProperties(g_thumbs[gidx], &dtp);
            }
            gidx++;
        }
        y += rows * SLOT_H + (rows - 1) * GAP + BOX_EXTRA + GROUP_GAP;
    }
}

/* ─── 计算窗口高度（DIP）：每组用**本组**列数 ─── */
static int CalcWinH(void)
{
    int gi, h = PAD_TOP;
    for (gi = 0; gi < g_grpCount; gi++)
    {
        int n = g_grp[gi].n;
        int columns = g_grp[gi].cols;
        int rows = (n + columns - 1) / columns;
        h += rows * SLOT_H + (rows - 1) * GAP + BOX_EXTRA;
    }
    h += (g_grpCount > 0 ? (g_grpCount - 1) * GROUP_GAP : 0) + HINT_H;
    return h;
}

/* ─── 清理：注销缩略图并隐藏窗口（窗口/页面复用，不销毁） ─── */
static void SwCleanup(void)
{
    int i;
    for (i = 0; i < MAX_TOTAL; i++)
        if (g_thumbs[i])
        {
            DwmUnregisterThumbnail(g_thumbs[i]);
            g_thumbs[i] = NULL;
        }
    if (g_swWin)
    {
        ReleaseCapture(); /* 兜底：隐藏前确保释放鼠标捕获 */
        ui_window_hide(g_swWin);
    }
    g_swTotal = 0;
}

/* ─── 显示切换器 ───────────────────────────────────── */
static void AltTabShow(void)
{
    char json[SW_JSON_MAX];
    UiWindowConfig cfg;
    wchar_t wpath[MAX_PATH];
    int gi, maxFit, capCols, monWDip, winW, winH, winWpx, winHpx, x, y;
    RECT mon;

    g_swDone = 0;
    g_swConfirm = 0;
    g_swPick = NULL;
    memset(g_targets, 0, sizeof(g_targets));
    memset(g_thumbs, 0, sizeof(g_thumbs));
    memset(g_cellHit, 0, sizeof(g_cellHit));

    CollectGroups();
    if (g_grpCount == 0)
    {
        g_swActive = 0;
        return;
    }

    /* 目标屏（钩子位置优先，否则当前光标） */
    g_swMon = MonitorFromPoint(g_swHookCursor, MONITOR_DEFAULTTONULL);
    if (!g_swMon || g_swMon == GetPrimaryMonitorHandle())
    {
        POINT pt;
        GetCursorPos(&pt);
        g_swMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
        if (!g_swMon)
            g_swMon = GetPrimaryMonitorHandle();
    }
    /* Core UI 初始化 + 窗口只建一次，之后复用（销毁会触发退出竞争）。
       注意：必须在取目标屏矩形/DPI 之前 init —— ui_init 才把本进程设为 PMv2，
       否则 GetMonitorInfo / GetDpiForMonitor 拿到的是虚拟化坐标。 */
    if (!g_swInited)
    {
        ui_init_with_theme(UI_THEME_DARK);
        g_swInited = 1;
    }
    if (!g_swPage)
    {
        /* 载入 switcher.uix（优先 <exe>\src\，其次 exe 同目录） */
        ResolveResPath("switcher.uix", wpath, MAX_PATH);
        g_swPage = ui_page_load_file(wpath);
        if (!g_swPage)
        {
            g_swActive = 0;
            return;
        }
        ZeroMemory(&cfg, sizeof(cfg));
        cfg.width = PAD_X * 2 + CELL_W;
        cfg.height = 300; /* 临时，稍后重设 */
        cfg.tool_window = 1;
        cfg.skip_animation = 1;
        g_swWin = ui_page_prepare_window(g_swPage, &cfg);
        if (!g_swWin)
        {
            ui_page_destroy(g_swPage);
            g_swPage = 0;
            g_swActive = 0;
            return;
        }
        g_swHwnd = (HWND)ui_window_hwnd(g_swWin);
        ui_window_on_key(g_swWin, OnKey, NULL);
        g_swOrigProc = (WNDPROC)SetWindowLongPtrW(g_swHwnd, GWLP_WNDPROC, (LONG_PTR)SwSubclassProc);
    }

    /* 目标屏 DPI + 矩形（须在 ui_init 之后：PMv2 生效后坐标才是物理像素） */
    g_swDpi = MonScale(g_swMon);
    GetMonitorRect(g_swMon, &mon);
    monWDip = (int)((mon.right - mon.left) / g_swDpi);

    /* 每组独立算列数：按屏幕能放下的数与 MAX_COLS 取上限，
       再在组内**平衡**成若干排（避免"最后一行只剩 1 个"）。 */
    maxFit = (monWDip - 2 * (PAD_X + BOX_PAD) + GAP) / (CELL_W + GAP);
    if (maxFit < 1)
        maxFit = 1;
    capCols = maxFit < MAX_COLS ? maxFit : MAX_COLS;
    for (gi = 0; gi < g_grpCount; gi++)
    {
        int n = g_grp[gi].n;
        int r = (n + capCols - 1) / capCols; /* 先按满排算行数 */
        int c = (n + r - 1) / r;             /* 再按行数平衡列数 */
        if (c > capCols)
            c = capCols;
        if (c < 1)
            c = 1;
        g_grp[gi].cols = c;
    }

    BuildJson(json);
    if (ui_page_set_json(g_swPage, "groups", json) != 0)
    {
        SwCleanup();
        g_swActive = 0;
        return;
    }
    ui_page_set_int(g_swPage, "sel", 0);
    if (g_swTotal == 0)
    {
        SwCleanup();
        g_swActive = 0;
        return;
    }

    /* 窗口宽度取**最大组**的卡片宽度 + 统一左右外边距；高度由各组 rows 累加。 */
    {
        int maxCardW = 0;
        for (gi = 0; gi < g_grpCount; gi++)
        {
            int cols = g_grp[gi].cols;
            int cw = 2 * BOX_PAD + cols * CELL_W + (cols - 1) * GAP;
            if (cw > maxCardW)
                maxCardW = cw;
        }
        winW = 2 * PAD_X + maxCardW;
    }
    winH = CalcWinH();

    /* 居中于目标屏 + 置顶 + 设最终尺寸（x/y=屏幕像素, w/h=DIP） */
    winWpx = (int)(winW * g_swDpi);
    winHpx = (int)(winH * g_swDpi);
    x = mon.left + ((mon.right - mon.left) - winWpx) / 2;
    y = mon.top + ((mon.bottom - mon.top) - winHpx) / 2;
    if (x < mon.left + 4)
        x = mon.left + 4;
    if (y < mon.top + 4)
        y = mon.top + 4;

    ui_window_set_rect(g_swWin, x, y, winW, winH); /* 先定尺寸 */
    ui_window_show_immediate(g_swWin);
    /* show 之后再原子地定位+定尺寸（物理像素）到目标屏正中 —— 之前"没居中"
       就是因为在 show 之前设的位置被 show 的默认摆放覆盖了。 */
    SetWindowPos(g_swHwnd, HWND_TOPMOST, x, y, winWpx, winHpx, SWP_NOACTIVATE);
    SetForegroundWindow(g_swHwnd);
    SetFocus(g_swHwnd);

    RegisterThumbs();

    g_swShown = 1;
    ui_run(); /* 嵌套消息循环，直到确认 / 取消 */
    g_swShown = 0;

    /* 确认：必要时把目标窗口移到目标屏并激活（已在该屏则不动） */
    if (g_swConfirm && g_swPick)
    {
        HMONITOR hMonW = MonitorFromWindow(g_swPick, MONITOR_DEFAULTTONEAREST);
        if (hMonW && g_swMon && hMonW != g_swMon)
            MoveWindowToMonitor(g_swPick, g_swMon, g_fullScreenMode);
        SwitchToThisWindow(g_swPick, TRUE);
    }

    SwCleanup();
    g_swActive = 0;
}

/* ═══════════════════════════════════════════════════════
 * 对外接口
 * ═══════════════════════════════════════════════════════ */

/* 兼容 wndproc.c 的调用；当前切换器不再使用自定义消息 */
int SwitcherHandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)msg;
    (void)wParam;
    (void)lParam;
    return 0;
}

/* 热键唤起：以**独立进程**启动切换器。
 *   这样 core-ui 的运行时内存(D3D11/D2D/DComp/QuickJS/页面)只在切换器进程存活
 *   期间占用，托盘本体不再常驻；关闭即随进程归还。单实例由子进程的互斥体保证。 */
void SwitcherStartFromHotkey(void)
{
    char exe[MAX_PATH];
    if (!g_cfg.SwitcherEnabled)
        return;
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    AllowSetForegroundWindow(ASFW_ANY); /* best-effort：让子进程能抢前台（SetCapture 需要） */
    ShellExecuteA(NULL, "open", exe, "--switcher", NULL, SW_SHOWNORMAL);
}

/* --switcher 入口：在独立进程里跑一次切换器。
 *   与 --settings 一样：刻意不设 SetProcessDPIAware，留给 core-ui 设 PMv2。 */
int RunSwitcherProcess(HINSTANCE hInst, int nCmdShow)
{
    HANDLE hMutex;
    (void)hInst;
    (void)nCmdShow;

    /* 单实例：已有一个切换器进程在跑就直接退出，避免热键连按开一堆 */
    hMutex = CreateMutexA(NULL, FALSE, "WindowMoveSwitcherMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hMutex);
        return 0;
    }

    LoadConfig();
    g_fullScreenMode = g_cfg.FullScreenMode; /* 供"把目标窗口移到目标屏"时决定是否全屏 */
    GetCursorPos(&g_swHookCursor);           /* 用唤起时的光标位置决定目标屏 */

    AltTabShow(); /* init core-ui + 显示 + ui_run + 激活目标 + 清理 */

    /* 独立进程：退出前释放 core-ui，内存随进程归还 */
    if (g_swWin)
    {
        ui_window_destroy(g_swWin);
        g_swWin = 0;
    }
    if (g_swPage)
    {
        ui_page_destroy(g_swPage);
        g_swPage = 0;
    }
    if (g_swInited)
    {
        ui_shutdown();
        g_swInited = 0;
    }

    CloseHandle(hMutex);
    return 0;
}