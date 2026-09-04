/* ============================================================================
 *  上网环境设置工具 v5.3  —  Win32 GUI（暗色科幻风）
 * ----------------------------------------------------------------------------
 *  功能：
 *    1. 内网固定 IP 模式    —— netsh 设置静态 IPv4 与 DNS
 *    2. 外网 DHCP 自动获取  —— netsh 切换为自动获取
 *    3. 实时查看当前网络配置
 *    4. 内置参数设置（界面内编辑，保存到注册表，无需外部配置文件）
 *  特性：
 *    - 启动自动请求管理员权限（requireAdministrator 清单）
 *    - 启动自动检测当前网络模式（内网固定IP / 外网DHCP）并高亮对应卡片
 *    - 单文件即用：默认参数内置，界面可修改并持久化到 HKCU\Software\IPSwitch
 *    - 运行日志带时间戳、可滚动（新日志在最下方），命令输出按 UTF-8 解码避免乱码
 *    - 深色科幻风界面，高 DPI 自适应
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <iphlpapi.h>
#include <wininet.h>
#include <urlmon.h>
#include <wincrypt.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* ------------------------------ 配置区 ----------------------------------- */
/* 默认参数（界面内可修改，持久化到注册表） */
static WCHAR g_Interface[64]  = L"以太网";
static WCHAR g_StaticIP[32]   = L"172.17.66.195";
static WCHAR g_SubnetMask[32] = L"255.255.255.240";
static WCHAR g_Gateway[32]    = L"172.17.66.193";
static WCHAR g_DnsServer[32]  = L"211.138.24.66";

#define REG_KEY L"Software\\IPSwitch"

/* ------------------------------ 在线更新配置 ------------------------------ */
/* 本工具版本号（与 update.json 的 version、Release tag 保持一致的基准） */
static const char g_version[] = "5.3.0";
/* GitHub 仓库 JSON（多源容错：jsDelivr 多节点 + 国内镜像，程序内 fetch_json 按序尝试） */
#define REL_CONFIG L"config.json"
#define REL_UPDATE L"update.json"
#define REL_NOTICE L"notice.json"
#define REL_TEXTS  L"texts.json"

/* ============================ 云端文本（texts.json） =======================
 * 所有用户可见的静态 UI 文本统一走 T(key)：
 *  - 启动后若成功拉到云端 texts.json，用云端文本覆盖对应 key；
 *  - 没网 / 拉取失败 / 未覆盖的 key，一律回退到内置默认文本。
 * ==========================================================================*/
typedef struct { const char *key; const WCHAR *def; WCHAR cur[256]; } TXT;
static TXT g_txt[] = {
    { "app_title",           L"上网环境设置工具", {0} },
    { "sub_title",           L"NETWORK SWITCHER  ·  V5.3  ·  内网/互联网一键切换", {0} },
    { "card_lan",            L"内网固定IP模式", {0} },
    { "card_wan",            L"外网DHCP自动获取", {0} },
    { "badge_applied",       L"已应用", {0} },
    { "field_ip",            L"IP", {0} },
    { "field_mask",          L"掩码", {0} },
    { "field_gw",            L"网关", {0} },
    { "field_dns",           L"DNS", {0} },
    { "field_auto",          L"自动获取", {0} },
    { "field_type",          L"适用", {0} },
    { "field_internet",      L"互联网", {0} },
    { "field_switch",        L"切换", {0} },
    { "field_switch_desc",   L"自动获取 IP 与 DNS", {0} },
    { "card_lan_hint",       L"点击卡片应用固定 IP 参数", {0} },
    { "card_wan_hint",       L"点击卡片切换为 DHCP 自动获取", {0} },
    { "btn_settings",        L"参数设置", {0} },
    { "chip_perm",           L"权限", {0} },
    { "chip_admin",          L"管理员", {0} },
    { "chip_limited",        L"受限", {0} },
    { "chip_nic",            L"网卡", {0} },
    { "chip_ip",             L"本机IP", {0} },
    { "btn_view",            L"查看当前网络配置", {0} },
    { "log_title",           L"运行日志 / CONSOLE", {0} },
    { "log_scroll",          L"滚轮翻阅", {0} },
    { "foot_note",           L"以管理员身份运行 · 参数设置自动保存，无需外部配置文件", {0} },
    { "settings_title",      L"网络参数设置", {0} },
    { "lbl_interface",       L"网卡名称", {0} },
    { "lbl_ip",              L"内网 IP", {0} },
    { "lbl_mask",            L"子网掩码", {0} },
    { "lbl_gw",              L"默认网关", {0} },
    { "lbl_dns",             L"DNS 服务器", {0} },
    { "btn_sync",            L"同步云端配置", {0} },
    { "btn_cancel",          L"取消", {0} },
    { "btn_save",            L"保存", {0} },
    { "ntc_update_title",    L"发现新版本", {0} },
    { "ntc_ann_title",       L"公告", {0} },
    { "btn_ok",              L"确定", {0} },
    { "btn_gotit",           L"知道了", {0} },
    { "btn_later",           L"稍后", {0} },
    { "ntc_dl_hint",         L"点击「确定」将自动下载最新版本，滚轮可下滑查看全文", {0} },
    { "err_ip",              L"IP 地址格式无效", {0} },
    { "err_mask",            L"子网掩码格式无效", {0} },
    { "err_gw",              L"默认网关格式无效", {0} },
    { "err_dns",             L"DNS 地址格式无效", {0} },
    { "err_ifc",             L"网卡名称不能为空", {0} },
    { "sync_loading",        L"正在从云端同步 ...", {0} },
    { "sync_fail",           L"同步失败：无法连接云端或格式无效", {0} },
    { "msg_upd_done",        L"新版本已下载完成。\n请关闭本程序后，用新文件替换旧版本即可。", {0} },
    { "msg_upd_title",       L"下载完成", {0} },
    { "msg_admin_req",       L"需要管理员权限运行本工具。\n请右键选择“以管理员身份运行”。", {0} },
};
#define TXT_N  ((int)(sizeof g_txt / sizeof g_txt[0]))

/* 取 key 对应文本：云端已覆盖返回云端文本，否则返回内置默认 */
static const WCHAR *T(const char *key) {
    for (int i = 0; i < TXT_N; i++)
        if (strcmp(g_txt[i].key, key) == 0)
            return g_txt[i].cur[0] ? g_txt[i].cur : g_txt[i].def;
    return L"?";
}

static HWND g_mainWnd = NULL;        /* 主窗口句柄（前向定义，公告区不再重复） */
static int json_str(const char *s, const char *key, char *out, int n);
static void unescape_newlines(char *s);

/* 用云端 texts.json 内容覆盖文本表，成功则重绘主窗口 */
static void apply_texts(const char *raw) {
    for (int i = 0; i < TXT_N; i++) {
        char v[256];
        if (json_str(raw, g_txt[i].key, v, sizeof v)) {
            unescape_newlines(v);   /* 还原 JSON 转义的 \n 为换行 */
            MultiByteToWideChar(CP_UTF8, 0, v, -1, g_txt[i].cur, 256);
        }
    }
    if (g_mainWnd) InvalidateRect(g_mainWnd, NULL, TRUE);
}

/* 公告状态（后台线程拉取后经 PostMessage 通知主线程） */
static volatile int g_pendingUpdate = 0;
static volatile int g_pendingNotice = 0;
static int  g_annShowing = 0;         /* 公告窗是否已打开 */
static int  g_lastNoticeId = 0;       /* 本地已看过的纯公告 id（注册表持久化） */

#define WM_APP_UPDATE  (WM_APP + 1)   /* 有更新公告 */
#define WM_APP_NOTICE  (WM_APP + 2)   /* 有纯公告 */
#define WM_APP_DL_DONE (WM_APP + 3)   /* 下载完成 */
#define WM_APP_SYNC_DONE (WM_APP + 4) /* 云端配置同步完成 */

/* 云端配置同步（后台线程结果，经 WM_APP_SYNC_DONE 通知主线程） */
static WCHAR g_syncIfc[64], g_syncIP[32], g_syncMask[32], g_syncGW[32], g_syncDNS[32];
static int   g_syncOk = 0;            /* 1=同步成功 */
static int   g_syncBusy = 0;          /* 1=正在同步 */

/* 后台线程填充 → 主线程经消息队列读取 */
static WCHAR g_updTitle[128], g_updContent[1024], g_updUrl[512], g_updMd5[64], g_updFile[64];
static WCHAR g_ntcTitle[128], g_ntcContent[1024];
static int   g_ntcId = 0;

/* 公告窗（当前展示内容）与下载结果 */
static int    g_ntType = 0;           /* 0=纯公告 1=更新公告 */
static WCHAR  g_ntTitle[128], g_ntContent[1024], g_ntUrl[512], g_ntMd5[64], g_ntFile[64];
static int    g_ntId = 0;
static WCHAR  g_dlPath[1024];
static int    g_ntScroll = 0;        /* 公告内容滚动偏移（字多时可下滑） */

/* ------------------------------ 颜色主题 ---------------------------------- */
#define C_BG       RGB(10, 16, 26)     /* 主背景        #0A101A */
#define C_PANEL    RGB(15, 23, 38)     /* 面板          #0F1726 */
#define C_CARD     RGB(17, 27, 45)     /* 卡片          #111B2D */
#define C_CARD_HOV RGB(24, 36, 60)     /* 卡片悬停      #18243C */
#define C_CONSOLE  RGB(6, 11, 18)      /* 终端底色      #060B12 */
#define C_ACCENT   RGB(0, 212, 255)    /* 主强调 青      #00D4FF */
#define C_ACCENT2  RGB(124, 108, 255)  /* 次强调 紫      #7C6CFF */
#define C_GREEN    RGB(52, 211, 153)   /* 成功          #34D399 */
#define C_RED      RGB(248, 113, 113)  /* 失败          #F87171 */
#define C_TEXT     RGB(229, 231, 235)  /* 主文本        #E5E7EB */
#define C_DIM      RGB(148, 163, 184)  /* 次要文本      #94A3B8 */
#define C_GRID     RGB(30, 41, 59)     /* 分隔/边框     #1E293B */
#define C_LOG      RGB(165, 195, 220)  /* 命令输出      #A5C3DC */
#define CLR_NONE   ((COLORREF)-1)      /* 不绘制填充/边框 */

/* ------------------------------ 全局状态 ---------------------------------- */
static HINSTANCE g_hInst;
static float     g_scale = 1.0f;
static HFONT     g_fTitle, g_fSub, g_fBody, g_fChip, g_fCardT, g_fCardM, g_fFoot, g_fLog;
static int       g_hover = -1;          /* -1 无, 0 固定IP卡, 1 DHCP卡, 2 刷新, 3 设置 */
static int       g_busy  = 0;
static int       g_cursor_on = 1;
static WCHAR     g_curIP[32] = L"--.--.--.--";
static int       g_admin = 0;

/* 布局对象（在 layout 中计算） */
typedef struct { RECT rc; int active; } CARD;
static CARD g_card[2];
static RECT g_refreshRC;
static RECT g_settingsRC;
static RECT g_logPanel;

/* 日志滚动 */
static int  g_scroll = 0;       /* 0 = 显示最新 */
static int  g_maxLines = 1;
static RECT g_sbTrack;          /* 滚动条轨道 */
static RECT g_sbThumb;          /* 滚动条滑块 */
static int  g_sbDrag = 0;       /* 正在拖动滑块 */

/* 设置窗口 */
static HWND g_settingsWin = NULL;
static int  g_settingsOpen = 0;

/* ------------------------------ 日志缓冲 ---------------------------------- */
#define LOG_CAP 300
#define LOG_LEN 340
typedef struct { WCHAR txt[LOG_LEN]; COLORREF col; } LOGT;
static LOGT g_log[LOG_CAP];
static int  g_log_n = 0;
static CRITICAL_SECTION g_logCs;

static void log_line(const WCHAR *s, COLORREF c) {
    EnterCriticalSection(&g_logCs);
    WCHAR full[LOG_LEN];
    SYSTEMTIME st; GetLocalTime(&st);
    _snwprintf(full, LOG_LEN - 1, L"[%02d:%02d:%02d] %s",
               st.wHour, st.wMinute, st.wSecond, s);
    full[LOG_LEN - 1] = 0;
    if (g_log_n < LOG_CAP) {
        wcsncpy(g_log[g_log_n].txt, full, LOG_LEN - 1);
        g_log[g_log_n].txt[LOG_LEN - 1] = 0;
        g_log[g_log_n].col = c;
        g_log_n++;
    } else {
        for (int i = 1; i < LOG_CAP; i++) {
            wcscpy(g_log[i - 1].txt, g_log[i].txt);
            g_log[i - 1].col = g_log[i].col;
        }
        wcsncpy(g_log[LOG_CAP - 1].txt, full, LOG_LEN - 1);
        g_log[LOG_CAP - 1].txt[LOG_LEN - 1] = 0;
        g_log[LOG_CAP - 1].col = c;
    }
    g_scroll = 0;  /* 新日志自动回到最新 */
    LeaveCriticalSection(&g_logCs);
}

static void logf(COLORREF c, const WCHAR *fmt, ...) {
    WCHAR b[LOG_LEN];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf(b, LOG_LEN - 1, fmt, ap);
    va_end(ap);
    b[LOG_LEN - 1] = 0;
    log_line(b, c);
}

/* ------------------------------ 工具函数 ---------------------------------- */
#define SC(x) ((int)((x) * g_scale + 0.5f))

static float dpi_scale_of(void) {
    UINT dpi = 96;
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (u) {
        typedef UINT (WINAPI *fn)(void);
        fn f = (fn)GetProcAddress(u, "GetDpiForSystem");
        if (f) dpi = f();
    }
    return dpi / 96.0f;
}

static int is_admin(void) {
    BOOL is = FALSE; HANDLE t = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) {
        TOKEN_ELEVATION te = {0}; DWORD sz = 0;
        if (GetTokenInformation(t, TokenElevation, &te, sizeof te, &sz))
            is = te.TokenIsElevated;
        CloseHandle(t);
    }
    return is ? 1 : 0;
}

static int valid_ip(const WCHAR *s) {
    if (!s || !*s) return 0;
    int dots = 0, digits = 0;
    for (const WCHAR *p = s; *p; p++) {
        if (*p == L'.') { dots++; digits = 0; }
        else if (*p >= L'0' && *p <= L'9') digits++;
        else return 0;
        if (dots > 3 || digits > 3) return 0;
    }
    return dots == 3;
}

static void trim_w(WCHAR *s) {
    WCHAR *e = s + wcslen(s);
    while (e > s && (e[-1] == L' ' || e[-1] == L'\t')) *--e = 0;
    WCHAR *b = s;
    while (*b == L' ' || *b == L'\t') b++;
    if (b != s) memmove(s, b, (wcslen(b) + 1) * sizeof(WCHAR));
}

/* ------------------------------ 在线工具（WinINet / 极简 JSON） ------------- */
/* 用 WinINet 拉取一个 URL 的正文，返回 UTF-8 字符串（需 free），失败返回 NULL */
static char *fetch_url(const WCHAR *url, int timeout_ms) {
    HINTERNET hI = InternetOpenW(L"netflip-x/1.0", INTERNET_OPEN_TYPE_PRECONFIG,
                                 NULL, NULL, 0);
    if (!hI) return NULL;
    DWORD t = (DWORD)timeout_ms;
    InternetSetOptionW(hI, INTERNET_OPTION_CONNECT_TIMEOUT, &t, sizeof t);
    InternetSetOptionW(hI, INTERNET_OPTION_RECEIVE_TIMEOUT, &t, sizeof t);
    HINTERNET hU = InternetOpenUrlW(hI, url, NULL, 0,
                                    INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                                    INTERNET_FLAG_SECURE, 0);
    if (!hU) { InternetCloseHandle(hI); return NULL; }
    char *acc = NULL; size_t accn = 0, cap = 0; char buf[2048]; DWORD n;
    while (InternetReadFile(hU, buf, sizeof buf, &n) && n > 0) {
        if (accn + n + 1 > cap) {
            size_t nc = (accn + n + 1) * 2 + 256;
            char *na = (char *)realloc(acc, nc);
            if (!na) break;
            acc = na; cap = nc;
        }
        memcpy(acc + accn, buf, n); accn += n;
    }
    InternetCloseHandle(hU);
    InternetCloseHandle(hI);
    if (acc && accn > 0) { acc[accn] = 0; return acc; }
    free(acc);
    return NULL;
}
/* 多源容错 + 内容校验：按序尝试直链 + jsDelivr 各节点（含国内镜像），
   每个源独立短超时（timeout_ms），全部试完；拉到内容后检查是否含关键字段
   need（如 "\"notice_id\""），不含则换下一个源。全部失败返回 NULL。
   注意：仅在后台线程调用，不会阻塞界面。 */
static char *fetch_json(const WCHAR *rel, const char *need, int timeout_ms) {
    static const WCHAR *bases[] = {
        L"https://github.com/2210638944/netflip-x/raw/refs/heads/main/",
        L"https://cdn.jsdelivr.net/gh/2210638944/netflip-x@main/",
        L"https://fastly.jsdelivr.net/gh/2210638944/netflip-x@main/",
        L"https://testingcf.jsdelivr.net/gh/2210638944/netflip-x@main/",
        L"https://gcore.jsdelivr.net/gh/2210638944/netflip-x@main/",
        L"https://cdn.jsdmirror.com/gh/2210638944/netflip-x@main/",
    };
    static const char *hosts[] = {
        "github.com(直链)", "cdn.jsdelivr.net", "fastly.jsdelivr.net", "testingcf.jsdelivr.net",
        "gcore.jsdelivr.net", "cdn.jsdmirror.com",
    };
    const int n = (int)(sizeof bases / sizeof bases[0]);
    WCHAR url[900];
    for (int i = 0; i < n; i++) {
        int cap = timeout_ms;
        if (i == 0 && cap > 2500) cap = 2500;   /* 直链限时 2.5s：通了秒回，不通立刻让位 */
        logf(C_DIM, L"[在线]   尝试源[%d/%d] %hs ...", i + 1, n, hosts[i]);
        _snwprintf(url, 900, L"%s%s?t=%lu", bases[i], rel, (unsigned long)GetTickCount());
        char *r = fetch_url(url, cap);
        if (!r) {
            logf(C_RED, L"[在线]      %hs 连接失败，换下一个源", hosts[i]);
            continue;
        }
        if (need && !strstr(r, need)) {
            logf(C_RED, L"[在线]      %hs 内容不符（非预期 JSON），换下一个源", hosts[i]);
            free(r);
            continue;
        }
        logf(C_GREEN, L"[在线]      %hs 拉取成功", hosts[i]);
        return r;
    }
    logf(C_RED, L"[在线] 全部 %d 个源均不可用（%ls）", n, rel);
    return NULL;
}

/* 极简扁平 JSON：取 "key":"value" 的字符串值（UTF-8），返回 1 成功 0 失败 */
static int json_str(const char *s, const char *key, char *out, int n) {
    char pat[96];
    _snprintf(pat, sizeof pat - 1, "\"%s\"", key);
    const char *f = strstr(s, pat);
    if (!f) return 0;
    const char *v = f + strlen(pat);
    while (*v == ' ' || *v == '\t') v++;
    if (*v != ':') return 0;
    v++;
    while (*v == ' ' || *v == '\t') v++;
    if (*v != '"') return 0;
    v++;
    const char *e = v;
    while (*e && *e != '"') e++;
    int len = (int)(e - v);
    if (len >= n) len = n - 1;
    memcpy(out, v, len); out[len] = 0;
    return 1;
}

/* 取 JSON 字段原始值：既支持带引号字符串，也支持裸 token（true/false/数字） */
static int json_raw(const char *s, const char *key, char *out, int n) {
    char pat[96];
    _snprintf(pat, sizeof pat - 1, "\"%s\"", key);
    const char *f = strstr(s, pat);
    if (!f) return 0;
    const char *v = f + strlen(pat);
    while (*v == ' ' || *v == '\t') v++;
    if (*v != ':') return 0;
    v++;
    while (*v == ' ' || *v == '\t') v++;
    const char *e;
    if (*v == '"') {
        v++; e = v;
        while (*e && *e != '"') e++;
    } else {
        e = v;
        while (*e && *e != ',' && *e != '}' && *e != '\r' && *e != '\n' && *e != ' ' && *e != '\t') e++;
    }
    int len = (int)(e - v);
    if (len >= n) len = n - 1;
    memcpy(out, v, len); out[len] = 0;
    return 1;
}

static int json_int(const char *s, const char *key, int def) {
    char buf[32];
    if (json_raw(s, key, buf, sizeof buf)) {
        char *end = NULL; long v = strtol(buf, &end, 10);
        if (end && *end == 0) return (int)v;
    }
    return def;
}

static int json_bool(const char *s, const char *key) {
    char buf[8];
    if (json_raw(s, key, buf, sizeof buf)) return _stricmp(buf, "true") == 0;
    return 0;
}

/* 把 JSON 字符串里的 "\n" 转义还原为换行（就地） */
static void unescape_newlines(char *s) {
    char *r = s;
    while (*s) {
        if (s[0] == '\\' && s[1] == 'n') { *r++ = '\n'; s += 2; }
        else *r++ = *s++;
    }
    *r = 0;
}

/* 版本号逐段数字比较：a>b>0，a<b<0，相等 0 */
static int cmp_version(const char *a, const char *b) {
    while (*a || *b) {
        int va = 0, vb = 0;
        while (*a >= '0' && *a <= '9') { va = va * 10 + (*a - '0'); a++; }
        while (*b >= '0' && *b <= '9') { vb = vb * 10 + (*b - '0'); b++; }
        if (va != vb) return va - vb;
        if (*a == '.') a++; if (*b == '.') b++;
    }
    return 0;
}

/* 计算文件 MD5（小写十六进制，40 位缓冲），成功 0 失败 -1 */
static int md5_file(const WCHAR *path, WCHAR *hexout) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    HCRYPTPROV prov = 0; HCRYPTHASH hh = 0;
    if (!CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        CloseHandle(f); return -1;
    }
    if (!CryptCreateHash(prov, CALG_MD5, 0, 0, &hh)) {
        CryptReleaseContext(prov, 0); CloseHandle(f); return -1;
    }
    BYTE buf[8192]; DWORD rd;
    while (ReadFile(f, buf, sizeof buf, &rd, NULL) && rd > 0)
        CryptHashData(hh, buf, rd, 0);
    CloseHandle(f);
    BYTE dig[16]; DWORD dsz = sizeof dig;
    CryptGetHashParam(hh, HP_HASHVAL, dig, &dsz, 0);
    CryptDestroyHash(hh); CryptReleaseContext(prov, 0);
    static const WCHAR *hex = L"0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        hexout[i * 2] = hex[dig[i] >> 4];
        hexout[i * 2 + 1] = hex[dig[i] & 15];
    }
    hexout[32] = 0;
    return 0;
}

/* 当前 exe 所在目录 */
static void get_exe_dir(WCHAR *out, int n) {
    GetModuleFileNameW(NULL, out, n);
    WCHAR *s = wcsrchr(out, L'\\');
    if (s) *s = 0;
}

/* 下载 URL 到本地文件（URLDownloadToFile），成功 0 */
static int download_file(const WCHAR *url, const WCHAR *path) {
    return SUCCEEDED(URLDownloadToFileW(NULL, url, path, 0, NULL)) ? 0 : -1;
}

/* ------------------------------ 注册表持久化 ------------------------------ */
static void save_config_reg(void) {
    HKEY k = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0,
                        KEY_WRITE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"Interface", 0, REG_SZ, (const BYTE *)g_Interface,
                       (DWORD)((wcslen(g_Interface) + 1) * sizeof(WCHAR)));
        RegSetValueExW(k, L"IP", 0, REG_SZ, (const BYTE *)g_StaticIP,
                       (DWORD)((wcslen(g_StaticIP) + 1) * sizeof(WCHAR)));
        RegSetValueExW(k, L"Mask", 0, REG_SZ, (const BYTE *)g_SubnetMask,
                       (DWORD)((wcslen(g_SubnetMask) + 1) * sizeof(WCHAR)));
        RegSetValueExW(k, L"Gateway", 0, REG_SZ, (const BYTE *)g_Gateway,
                       (DWORD)((wcslen(g_Gateway) + 1) * sizeof(WCHAR)));
        RegSetValueExW(k, L"Dns", 0, REG_SZ, (const BYTE *)g_DnsServer,
                       (DWORD)((wcslen(g_DnsServer) + 1) * sizeof(WCHAR)));
        RegCloseKey(k);
    }
}

static void read_reg_str(HKEY k, const WCHAR *name, WCHAR *out, size_t n) {
    DWORD sz = (DWORD)(n * sizeof(WCHAR));
    DWORD t = 0;
    if (RegQueryValueExW(k, name, NULL, &t, (BYTE *)out, &sz) != ERROR_SUCCESS)
        return;
    if (t != REG_SZ || sz < sizeof(WCHAR)) return;
    out[n - 1] = 0;
}

static void load_config_reg(void) {
    HKEY k = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return;
    read_reg_str(k, L"Interface", g_Interface, 64);
    read_reg_str(k, L"IP", g_StaticIP, 32);
    read_reg_str(k, L"Mask", g_SubnetMask, 32);
    read_reg_str(k, L"Gateway", g_Gateway, 32);
    read_reg_str(k, L"Dns", g_DnsServer, 32);
    DWORD v = 0, vsz = sizeof v; DWORD vt = 0;
    if (RegQueryValueExW(k, L"NoticeId", NULL, &vt, (BYTE *)&v, &vsz) == ERROR_SUCCESS && vt == REG_DWORD)
        g_lastNoticeId = (int)v;
    RegCloseKey(k);
}

static void save_last_notice_id(void) {
    HKEY k = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0,
                        KEY_WRITE, NULL, &k, NULL) == ERROR_SUCCESS) {
        DWORD v = (DWORD)g_lastNoticeId;
        RegSetValueExW(k, L"NoticeId", 0, REG_DWORD, (const BYTE *)&v, sizeof v);
        RegCloseKey(k);
    }
}

/* ------------------------------ 命令执行 ---------------------------------- */
/* 包装为 cmd /c chcp 65001 以确保输出为 UTF-8，从而正确显示中文 */
static DWORD run_cmd_capture(const WCHAR *cmdline, WCHAR **outp) {
    *outp = NULL;
    HANDLE rp = NULL, wp = NULL;
    SECURITY_ATTRIBUTES sa; sa.nLength = sizeof sa; sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = NULL;
    if (!CreatePipe(&rp, &wp, &sa, 0)) return (DWORD)-1;
    SetHandleInformation(rp, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wp; si.hStdError = wp;
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdInput = (hin == INVALID_HANDLE_VALUE) ? NULL : hin;

    size_t need = wcslen(cmdline) + 64;
    WCHAR *wrapped = (WCHAR *)malloc(need * sizeof(WCHAR));
    _snwprintf(wrapped, need, L"cmd.exe /d /c chcp 65001>nul & %s", cmdline);

    BOOL ok = CreateProcessW(NULL, wrapped, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                             NULL, NULL, &si, &pi);
    free(wrapped);
    CloseHandle(wp);
    if (!ok) { CloseHandle(rp); return (DWORD)-1; }
    CloseHandle(pi.hThread);

    char *acc = NULL; size_t accn = 0, cap = 0; char buf[4096]; DWORD n;
    while (ReadFile(rp, buf, sizeof buf, &n, NULL) && n > 0) {
        if (accn + n + 1 > cap) {
            size_t nc = (accn + n + 1) * 2 + 256;
            char *na = (char *)realloc(acc, nc);
            if (!na) break;
            acc = na; cap = nc;
        }
        memcpy(acc + accn, buf, n); accn += n;
    }
    CloseHandle(rp);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 0; GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess);

    if (acc && accn > 0) {
        acc[accn] = 0;
        /* 优先按 UTF-8 解码，失败则退回系统 ACP */
        int wn = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, acc, (int)accn, NULL, 0);
        if (wn <= 0)
            wn = MultiByteToWideChar(CP_ACP, 0, acc, (int)accn, NULL, 0);
        if (wn > 0) {
            WCHAR *w = (WCHAR *)malloc((wn + 1) * sizeof(WCHAR));
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, acc, (int)accn, w, wn) <= 0)
                MultiByteToWideChar(CP_ACP, 0, acc, (int)accn, w, wn);
            w[wn] = 0; *outp = w;
        }
    }
    free(acc);
    return ec;
}

/* 执行命令并把输出逐行写入日志 */
static DWORD run_cmd(const WCHAR *cmdline) {
    logf(C_ACCENT, L"> %s", cmdline);
    WCHAR *out = NULL;
    DWORD ec = run_cmd_capture(cmdline, &out);
    if (out) {
        WCHAR *s = out;
        while (*s) {
            WCHAR *e = wcschr(s, L'\n');
            if (e) *e = 0;
            WCHAR *t = s + wcslen(s);
            while (t > s && (t[-1] == L'\r' || t[-1] == L' ')) *--t = 0;
            if (*s) log_line(s, C_LOG);
            if (!e) break;
            s = e + 1;
        }
        free(out);
    } else if (ec == (DWORD)-1) {
        log_line(L"!! 命令无法启动", C_RED);
    }
    return ec;
}

/* ------------------------------ 核心功能 ---------------------------------- */
static void refresh_config(void);
static DWORD WINAPI online_check_thread(LPVOID p);
static int detect_current_mode(void);

static void apply_static(void) {
    g_busy = 1;
    WCHAR cmd[700];
    log_line(L"", C_GRID);
    logf(C_ACCENT, L"应用模式：内网固定 IP");
    _snwprintf(cmd, 700, L"netsh interface ipv4 set address name=\"%s\" source=static addr=%s mask=%s gateway=%s gwmetric=0",
             g_Interface, g_StaticIP, g_SubnetMask, g_Gateway);
    DWORD ec = run_cmd(cmd);
    if (ec == 0) logf(C_GREEN, L"[OK] IP 地址已设置：%s", g_StaticIP);
    else         logf(C_RED,   L"[FAIL] IP 设置失败（代码 %lu）", ec);
    _snwprintf(cmd, 700, L"netsh interface ipv4 set dns name=\"%s\" source=static addr=%s register=PRIMARY",
             g_Interface, g_DnsServer);
    ec = run_cmd(cmd);
    if (ec == 0) logf(C_GREEN, L"[OK] DNS 已设置：%s", g_DnsServer);
    else         logf(C_RED,   L"[FAIL] DNS 设置失败（代码 %lu）", ec);
    g_card[0].active = 1; g_card[1].active = 0;
    refresh_config();
    g_busy = 0;
}

static void apply_dhcp(void) {
    g_busy = 1;
    WCHAR cmd[700];
    log_line(L"", C_GRID);
    logf(C_ACCENT2, L"应用模式：外网 DHCP 自动获取");
    _snwprintf(cmd, 700, L"netsh interface ipv4 set address name=\"%s\" source=dhcp", g_Interface);
    DWORD ec = run_cmd(cmd);
    if (ec == 0) logf(C_GREEN, L"[OK] IP 已切换为自动获取");
    else         logf(C_RED,   L"[FAIL] IP 切换失败（代码 %lu）", ec);
    _snwprintf(cmd, 700, L"netsh interface ipv4 set dns name=\"%s\" source=dhcp", g_Interface);
    ec = run_cmd(cmd);
    if (ec == 0) logf(C_GREEN, L"[OK] DNS 已切换为自动获取");
    else         logf(C_RED,   L"[FAIL] DNS 切换失败（代码 %lu）", ec);
    g_card[1].active = 1; g_card[0].active = 0;
    refresh_config();
    g_busy = 0;
    /* 切到互联网模式后自动重新检查公告与更新（无需重启程序） */
    if (detect_current_mode() == 1) {
        log_line(L"已切换至互联网模式，重新检查公告与更新 ...", C_DIM);
        HANDLE th = CreateThread(NULL, 0, online_check_thread, g_mainWnd, 0, NULL);
        if (th) CloseHandle(th);
    }
}

static void refresh_config(void) {
    log_line(L"- 当前网络配置：", C_DIM);
    WCHAR *out = NULL;
    DWORD ec = run_cmd_capture(L"ipconfig", &out);
    (void)ec;
    if (out) {
        WCHAR *s = out;
        while (*s) {
            WCHAR *e = wcschr(s, L'\n');
            if (e) *e = 0;
            WCHAR *t = s + wcslen(s);
            while (t > s && (t[-1] == L'\r' || t[-1] == L' ')) *--t = 0;
            if (*s) {
                int show = 0;
                if (wcsstr(s, L"IPv4"))   show = 1;
                if (wcsstr(s, L"IPv6"))   show = 0;
                if (wcsstr(s, L"子网掩码") || wcsstr(s, L"Subnet Mask")) show = 1;
                if (wcsstr(s, L"默认网关") || wcsstr(s, L"Default Gateway")) show = 1;
                if (wcsstr(s, L"DNS 服务器") || wcsstr(s, L"DNS Servers")) show = 1;
                if (wcsstr(s, L"以太网适配器") || wcsstr(s, L"Ethernet adapter") ||
                    wcsstr(s, L"无线局域网适配器") || wcsstr(s, L"Wireless LAN adapter")) show = 1;
                if (show) log_line(s, C_GREEN);
                if (wcsstr(s, L"IPv4")) {
                    WCHAR *colon = wcsrchr(s, L':');
                    if (colon) {
                        WCHAR *v = colon + 1;
                        while (*v == L' ') v++;
                        wcsncpy(g_curIP, v, 31); g_curIP[31] = 0;
                    }
                }
            }
            if (!e) break;
            s = e + 1;
        }
        free(out);
    }
}

/* ------------------------------ 布局 ------------------------------------- */
/* 启动/应用后自动检测当前网络模式：
 *   返回 0 = 内网固定IP（静态）, 1 = 外网DHCP（自动获取）, -1 = 未找到网卡 */
static int detect_current_mode(void) {
    ULONG sz = 0;
    if (GetAdaptersAddresses(AF_INET, 0, NULL, NULL, &sz) != ERROR_BUFFER_OVERFLOW || sz == 0)
        return -1;
    IP_ADAPTER_ADDRESSES *aa = (IP_ADAPTER_ADDRESSES *)malloc(sz);
    if (!aa) return -1;
    ULONG ifidx = 0;
    if (GetAdaptersAddresses(AF_INET, 0, NULL, aa, &sz) == NO_ERROR) {
        for (IP_ADAPTER_ADDRESSES *p = aa; p; p = p->Next) {
            if (p->FriendlyName && wcscmp(p->FriendlyName, g_Interface) == 0) {
                ifidx = p->IfIndex;
                break;
            }
        }
    }
    free(aa);
    if (!ifidx) return -1;

    sz = 0;
    if (GetAdaptersInfo(NULL, &sz) != ERROR_BUFFER_OVERFLOW || sz == 0)
        return -1;
    IP_ADAPTER_INFO *ai = (IP_ADAPTER_INFO *)malloc(sz);
    if (!ai) return -1;
    int mode = -1;
    if (GetAdaptersInfo(ai, &sz) == NO_ERROR) {
        for (IP_ADAPTER_INFO *p = ai; p; p = p->Next) {
            if (p->Index == ifidx) {
                mode = p->DhcpEnabled ? 1 : 0;
                /* 顺带刷新本机当前 IP */
                if (p->IpAddressList.IpAddress.String[0]) {
                    MultiByteToWideChar(CP_ACP, 0, p->IpAddressList.IpAddress.String, -1,
                                        g_curIP, 31);
                    g_curIP[31] = 0;
                }
                break;
            }
        }
    }
    free(ai);
    return mode;
}

static void mark_current_mode(void) {
    int m = detect_current_mode();
    if (m == 0) {
        g_card[0].active = 1; g_card[1].active = 0;
        logf(C_GREEN, L"当前模式：内网固定 IP（静态）");
    } else if (m == 1) {
        g_card[1].active = 1; g_card[0].active = 0;
        logf(C_GREEN, L"当前模式：外网 DHCP 自动获取（互联网）");
    } else {
        g_card[0].active = g_card[1].active = 0;
        logf(C_RED, L"未找到网卡“%s”，请在参数设置中检查网卡名称", g_Interface);
    }
}

static void layout(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right;
    int cw = SC(430), ch = SC(176), gap = SC(24);
    int x0 = (W - (2 * cw + gap)) / 2;
    int y0 = SC(158);
    SetRect(&g_card[0].rc, x0, y0, x0 + cw, y0 + ch);
    SetRect(&g_card[1].rc, x0 + cw + gap, y0, x0 + 2 * cw + gap, y0 + ch);
    SetRect(&g_refreshRC, x0, SC(346), x0 + 2 * cw + gap, SC(346) + SC(40));
    SetRect(&g_settingsRC, W - SC(40) - SC(96), SC(22), W - SC(40), SC(22) + SC(32));
    SetRect(&g_logPanel, SC(40), SC(392), W - SC(40), SC(570));
}

/* ------------------------------ 绘制 ------------------------------------- */
static HFONT make_font(const WCHAR *face, int px, int weight) {
    int h = -(int)(px * g_scale + 0.5f);
    return CreateFontW(h, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

static void init_fonts(void) {
    g_fTitle  = make_font(L"Segoe UI", 26, FW_BOLD);
    g_fSub    = make_font(L"Consolas", 13, FW_NORMAL);
    g_fBody   = make_font(L"Segoe UI", 13, FW_NORMAL);
    g_fChip   = make_font(L"Segoe UI", 12, FW_NORMAL);
    g_fCardT  = make_font(L"Segoe UI", 19, FW_BOLD);
    g_fCardM  = make_font(L"Consolas", 13, FW_NORMAL);
    g_fFoot   = make_font(L"Segoe UI", 11, FW_NORMAL);
    g_fLog    = make_font(L"Consolas", 13, FW_NORMAL);
}

static void destroy_fonts(void) {
    DeleteObject(g_fTitle); DeleteObject(g_fSub); DeleteObject(g_fBody);
    DeleteObject(g_fChip); DeleteObject(g_fCardT); DeleteObject(g_fCardM);
    DeleteObject(g_fFoot); DeleteObject(g_fLog);
}

static void fill(HDC hdc, int x, int y, int w, int h, COLORREF c) {
    RECT r = { x, y, x + w, y + h };
    HBRUSH b = CreateSolidBrush(c);
    FillRect(hdc, &r, b);
    DeleteObject(b);
}

static void text(HDC hdc, int x, int y, int w, int h, const WCHAR *s,
                 HFONT f, COLORREF c, UINT fmt) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, c);
    SelectObject(hdc, f);
    RECT r = { x, y, x + w, y + h };
    DrawTextW(hdc, s, -1, &r, fmt | DT_NOPREFIX);
}

static void rounded(HDC hdc, int x, int y, int w, int h, int rad,
                    COLORREF fillc, COLORREF bordc, int bwd) {
    HRGN rg = CreateRoundRectRgn(x, y, x + w, y + h, rad * 2, rad * 2);
    if (fillc != CLR_NONE) {
        HBRUSH fb = CreateSolidBrush(fillc);
        FillRgn(hdc, rg, fb);
        DeleteObject(fb);
    }
    if (bordc != CLR_NONE && bwd > 0) {
        HBRUSH bb = CreateSolidBrush(bordc);
        FrameRgn(hdc, rg, bb, bwd, bwd);
        DeleteObject(bb);
    }
    DeleteObject(rg);
}

static void hgradient(HDC hdc, int x, int y, int w, int h, COLORREF c1, COLORREF c2) {
    int r1 = GetRValue(c1), g1 = GetGValue(c1), b1 = GetBValue(c1);
    int r2 = GetRValue(c2), g2 = GetGValue(c2), b2 = GetBValue(c2);
    for (int i = 0; i < w; i++) {
        double t = (w == 1) ? 0 : (double)i / (w - 1);
        COLORREF c = RGB((int)(r1 + (r2 - r1) * t + .5),
                         (int)(g1 + (g2 - g1) * t + .5),
                         (int)(b1 + (b2 - b1) * t + .5));
        HPEN q = CreatePen(PS_SOLID, 1, c);
        SelectObject(hdc, q);
        MoveToEx(hdc, x + i, y, NULL);
        LineTo(hdc, x + i, y + h);
        DeleteObject(q);
    }
}

static COLORREF lighten(COLORREF c, int amt) {
    int r = min(255, GetRValue(c) + amt);
    int g = min(255, GetGValue(c) + amt);
    int b = min(255, GetBValue(c) + amt);
    return RGB(r, g, b);
}

static int text_w(HDC hdc, const WCHAR *s) {
    SIZE sz; GetTextExtentPoint32W(hdc, s, lstrlenW(s), &sz); return sz.cx;
}

static void draw_chip(HDC hdc, int *x, const WCHAR *label, const WCHAR *value, COLORREF vc) {
    int h = SC(28);
    SelectObject(hdc, g_fChip);
    int lw = text_w(hdc, label) + SC(6);
    int vw = text_w(hdc, value) + SC(6);
    int w = lw + vw + SC(26);
    rounded(hdc, *x, SC(116), w, h, SC(10), C_PANEL, C_GRID, 1);
    text(hdc, *x + SC(12), SC(116), lw, h, label, g_fChip, C_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    text(hdc, *x + SC(12) + lw, SC(116), vw, h, value, g_fChip, vc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    *x += w + SC(12);
}

static void draw_card(HDC hdc, int i) {
    CARD *c = &g_card[i];
    int x = c->rc.left, y = c->rc.top, w = c->rc.right - c->rc.left, h = c->rc.bottom - c->rc.top;
    COLORREF accent = (i == 0) ? C_ACCENT : C_ACCENT2;
    COLORREF bg = (g_hover == i) ? C_CARD_HOV : C_CARD;
    COLORREF border = c->active ? C_GREEN : (g_hover == i ? lighten(accent, 30) : C_GRID);
    rounded(hdc, x, y, w, h, SC(16), bg, border, SC(2));
    /* 左侧强调条 */
    fill(hdc, x + SC(12), y + SC(18), SC(3), h - SC(36), accent);
    int tx = x + SC(30), tw = w - SC(56);
    text(hdc, tx, y + SC(16), tw, SC(30),
         i == 0 ? T("card_lan") : T("card_wan"), g_fCardT, C_TEXT,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (c->active) {
        int bw = SC(58), bh = SC(24);
        rounded(hdc, x + w - bw - SC(16), y + SC(18), bw, bh, SC(12), C_GREEN, CLR_NONE, 0);
        text(hdc, x + w - bw - SC(16), y + SC(18), bw, bh, T("badge_applied"), g_fChip, RGB(6, 40, 30),
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (i == 0) {
        text(hdc, tx, y + SC(50), tw, SC(20), T("field_ip"), g_fCardM, C_DIM, DT_LEFT | DT_SINGLELINE);
        text(hdc, tx + SC(120), y + SC(50), tw - SC(120), SC(20), g_StaticIP, g_fCardM, accent, DT_LEFT | DT_SINGLELINE);
        text(hdc, tx, y + SC(74), tw, SC(20), T("field_mask"), g_fCardM, C_DIM, DT_LEFT | DT_SINGLELINE);
        text(hdc, tx + SC(120), y + SC(74), tw - SC(120), SC(20), g_SubnetMask, g_fCardM, C_TEXT, DT_LEFT | DT_SINGLELINE);
        text(hdc, tx, y + SC(98), tw, SC(20), T("field_gw"), g_fCardM, C_DIM, DT_LEFT | DT_SINGLELINE);
        text(hdc, tx + SC(120), y + SC(98), tw - SC(120), SC(20), g_Gateway, g_fCardM, C_TEXT, DT_LEFT | DT_SINGLELINE);
        text(hdc, tx, y + SC(122), tw, SC(20), T("field_dns"), g_fCardM, C_DIM, DT_LEFT | DT_SINGLELINE);
        text(hdc, tx + SC(120), y + SC(122), tw - SC(120), SC(20), g_DnsServer, g_fCardM, accent, DT_LEFT | DT_SINGLELINE);
    } else {
        text(hdc, tx, y + SC(50), tw, SC(20), T("field_ip"), g_fCardM, C_DIM, DT_LEFT | DT_SINGLELINE);
        text(hdc, tx + SC(120), y + SC(50), tw - SC(120), SC(20), T("field_auto"), g_fCardM, accent, DT_LEFT | DT_SINGLELINE);
        text(hdc, tx, y + SC(74), tw, SC(20), T("field_dns"), g_fCardM, C_DIM, DT_LEFT | DT_SINGLELINE);
        text(hdc, tx + SC(120), y + SC(74), tw - SC(120), SC(20), T("field_auto"), g_fCardM, accent, DT_LEFT | DT_SINGLELINE);
        text(hdc, tx, y + SC(98), tw, SC(20), T("field_type"), g_fCardM, C_DIM, DT_LEFT | DT_SINGLELINE);
        text(hdc, tx + SC(120), y + SC(98), tw - SC(120), SC(20), T("field_internet"), g_fCardM, accent, DT_LEFT | DT_SINGLELINE);
        text(hdc, tx, y + SC(122), tw, SC(20), T("field_switch"), g_fCardM, C_DIM, DT_LEFT | DT_SINGLELINE);
        text(hdc, tx + SC(120), y + SC(122), tw - SC(120), SC(20), T("field_switch_desc"), g_fCardM, C_TEXT, DT_LEFT | DT_SINGLELINE);
    }
    /* 底部提示 */
    text(hdc, tx, y + h - SC(30), tw, SC(20),
         i == 0 ? T("card_lan_hint") : T("card_wan_hint"),
         g_fCardM, C_DIM, DT_LEFT | DT_SINGLELINE);
}

static void draw_console(HDC hdc, RECT cr) {
    fill(hdc, cr.left, cr.top, cr.right - cr.left, cr.bottom - cr.top, C_CONSOLE);
    EnterCriticalSection(&g_logCs);

    int lineH = SC(19);
    int availH = (cr.bottom - cr.top) - SC(6);
    g_maxLines = availH / lineH;
    if (g_maxLines < 1) g_maxLines = 1;

    int total = g_log_n;
    int overflow = total - g_maxLines;
    if (overflow < 0) overflow = 0;
    if (g_scroll > overflow) g_scroll = overflow;

    int textRight = cr.right - SC(4);
    if (overflow > 0) textRight -= SC(12);  /* 预留滚动条空间 */

    int start = total - g_maxLines - g_scroll;
    if (start < 0) start = 0;
    int yy = cr.top + SC(3);
    for (int i = start; i < start + g_maxLines && i < total; i++) {
        text(hdc, cr.left + SC(8), yy, textRight - cr.left - SC(8), lineH,
             g_log[i].txt, g_fLog, g_log[i].col,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        yy += lineH;
    }

    LeaveCriticalSection(&g_logCs);
    /* 光标（仅最新一行时闪烁） */
    if (g_scroll == 0 && total > 0 && g_cursor_on) {
        fill(hdc, cr.left + SC(8), yy + SC(2), SC(8), lineH - SC(4), C_ACCENT);
    }

    /* 滚动条 */
    if (overflow > 0) {
        int tx = cr.right - SC(11), tw = SC(5);
        SetRect(&g_sbTrack, tx, cr.top + SC(2), tx + tw, cr.bottom - SC(2));
        rounded(hdc, g_sbTrack.left, g_sbTrack.top,
                g_sbTrack.right - g_sbTrack.left, g_sbTrack.bottom - g_sbTrack.top,
                SC(2), RGB(17, 26, 40), CLR_NONE, 0);
        int th = g_sbTrack.bottom - g_sbTrack.top;
        int thumbH = th * g_maxLines / total;
        if (thumbH < SC(16)) thumbH = SC(16);
        int maxOff = th - thumbH;
        int off = (overflow > 0) ? (maxOff * g_scroll / overflow) : 0;
        SetRect(&g_sbThumb, g_sbTrack.left, g_sbTrack.top + off,
                g_sbTrack.right, g_sbTrack.top + off + thumbH);
        rounded(hdc, g_sbThumb.left, g_sbThumb.top,
                g_sbThumb.right - g_sbThumb.left, g_sbThumb.bottom - g_sbThumb.top,
                SC(2), C_ACCENT2, CLR_NONE, 0);
    }
}

static void draw(HDC hdc, HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    fill(hdc, 0, 0, W, H, C_BG);

    /* 头部 */
    text(hdc, SC(40), SC(20), SC(420), SC(42), T("app_title"), g_fTitle, C_ACCENT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    text(hdc, SC(40), SC(64), SC(520), SC(22), T("sub_title"), g_fSub, C_DIM, DT_LEFT | DT_SINGLELINE);
    hgradient(hdc, SC(40), SC(96), W - SC(80), SC(2), C_ACCENT, C_ACCENT2);

    /* 参数设置按钮（右上角） */
    COLORREF sbg = (g_hover == 3) ? C_CARD_HOV : C_PANEL;
    rounded(hdc, g_settingsRC.left, g_settingsRC.top,
            g_settingsRC.right - g_settingsRC.left, g_settingsRC.bottom - g_settingsRC.top,
            SC(8), sbg, g_hover == 3 ? lighten(C_ACCENT2, 30) : C_GRID, 1);
    text(hdc, g_settingsRC.left, g_settingsRC.top,
         g_settingsRC.right - g_settingsRC.left, g_settingsRC.bottom - g_settingsRC.top,
         T("btn_settings"), g_fChip, C_ACCENT2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* 状态芯片 */
    int x = SC(40);
    draw_chip(hdc, &x, T("chip_perm"), g_admin ? T("chip_admin") : T("chip_limited"), g_admin ? C_GREEN : C_RED);
    draw_chip(hdc, &x, T("chip_nic"), g_Interface, C_ACCENT);
    draw_chip(hdc, &x, T("chip_ip"), g_curIP, C_ACCENT);

    /* 模式卡片 */
    draw_card(hdc, 0);
    draw_card(hdc, 1);

    /* 查看当前网络配置按钮 */
    COLORREF rbg = (g_hover == 2) ? C_CARD_HOV : C_PANEL;
    rounded(hdc, g_refreshRC.left, g_refreshRC.top,
            g_refreshRC.right - g_refreshRC.left, g_refreshRC.bottom - g_refreshRC.top,
            SC(10), rbg, g_hover == 2 ? lighten(C_ACCENT, 20) : C_GRID, 1);
    text(hdc, g_refreshRC.left, g_refreshRC.top,
         g_refreshRC.right - g_refreshRC.left, g_refreshRC.bottom - g_refreshRC.top,
         T("btn_view"), g_fCardM, C_ACCENT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* 日志面板 */
    rounded(hdc, g_logPanel.left, g_logPanel.top,
            g_logPanel.right - g_logPanel.left, g_logPanel.bottom - g_logPanel.top,
            SC(12), C_PANEL, C_GRID, 1);
    text(hdc, g_logPanel.left + SC(18), g_logPanel.top + SC(10),
         SC(300), SC(20), T("log_title"), g_fChip, C_DIM, DT_LEFT | DT_SINGLELINE);
    text(hdc, g_logPanel.right - SC(150), g_logPanel.top + SC(10),
         SC(130), SC(20), T("log_scroll"), g_fChip, g_cursor_on ? C_GREEN : C_DIM, DT_RIGHT | DT_SINGLELINE);

    RECT cr = { g_logPanel.left + SC(14), g_logPanel.top + SC(38),
                g_logPanel.right - SC(14), g_logPanel.bottom - SC(12) };
    draw_console(hdc, cr);

    /* 底部说明 */
    text(hdc, 0, H - SC(26), W, SC(20),
         T("foot_note"),
         g_fFoot, C_DIM, DT_CENTER | DT_SINGLELINE);
}

/* ============================================================================
 *  参数设置窗口
 * ==========================================================================*/
#define IDC_EDIT_NIC   1001
#define IDC_EDIT_IP    1002
#define IDC_EDIT_MASK  1003
#define IDC_EDIT_GW    1004
#define IDC_EDIT_DNS   1005
#define IDC_SAVE       2001
#define IDC_CANCEL     2002
#define IDC_DEFAULT    2003

static HWND g_sEdits[5];
static WNDPROC g_sEditOrig[5];
static HFONT g_sFont;
static HBRUSH g_sBrushPanel, g_sBrushEdit;
static WCHAR g_sErr[96] = L"";

static void s_close(HWND hwnd);

static LRESULT CALLBACK s_EditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    int idx = -1;
    for (int i = 0; i < 5; i++)
        if (g_sEdits[i] == h) { idx = i; break; }

    if (m == WM_KEYDOWN) {
        if (w == VK_RETURN) { PostMessageW(g_settingsWin, WM_COMMAND, IDC_SAVE, 0); return 0; }
        if (w == VK_ESCAPE) { PostMessageW(g_settingsWin, WM_COMMAND, IDC_CANCEL, 0); return 0; }
    }

    /* IP 类字段（下标 1..4）：仅允许数字与点 */
    if (idx >= 1 && idx <= 4) {
        if (m == WM_CHAR) {
            WCHAR ch = (WCHAR)w;
            if (ch >= 32 && !((ch >= L'0' && ch <= L'9') || ch == L'.'))
                return 0;  /* 拦截非法字符 */
        }
        if (m == WM_PASTE) {
            if (OpenClipboard(h)) {
                HANDLE hd = GetClipboardData(CF_UNICODETEXT);
                if (hd) {
                    const WCHAR *src = (const WCHAR *)GlobalLock(hd);
                    if (src) {
                        WCHAR buf[128]; int n = 0;
                        for (const WCHAR *p = src; *p && n < 127; p++)
                            if ((*p >= L'0' && *p <= L'9') || *p == L'.')
                                buf[n++] = *p;
                        buf[n] = 0;
                        GlobalUnlock(hd);
                        SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)buf);
                        CloseClipboard();
                        return 0;
                    }
                }
                CloseClipboard();
            }
            return 0;
        }
    }

    WNDPROC o = (idx >= 0) ? g_sEditOrig[idx] : NULL;
    return o ? CallWindowProcW(o, h, m, w, l) : DefWindowProcW(h, m, w, l);
}

static HWND s_make_edit(HWND parent, int id, int x, int y, int w, int h, const WCHAR *val) {
    HWND e = CreateWindowW(L"EDIT", val,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT,
                           x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageW(e, WM_SETFONT, (WPARAM)g_sFont, TRUE);
    SendMessageW(e, EM_SETLIMITTEXT, (id == IDC_EDIT_NIC) ? 63 : 15, 0);
    g_sEditOrig[id - IDC_EDIT_NIC] = (WNDPROC)SetWindowLongPtrW(e, GWLP_WNDPROC, (LONG_PTR)s_EditProc);
    g_sEdits[id - IDC_EDIT_NIC] = e;
    return e;
}

static HWND s_make_btn(HWND parent, int id, const WCHAR *label, int x, int y, int w, int h) {
    HWND b = CreateWindowW(L"BUTTON", label,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                           x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageW(b, WM_SETFONT, (WPARAM)g_sFont, TRUE);
    return b;
}

static void s_draw_btn(HDC hdc, RECT *r, const WCHAR *label, COLORREF fillc, COLORREF tcol, int pressed) {
    rounded(hdc, r->left, r->top, r->right - r->left, r->bottom - r->top, SC(8),
            pressed ? lighten(fillc, 14) : fillc,
            pressed ? lighten(tcol, 40) : C_GRID, 1);
    text(hdc, r->left, r->top, r->right - r->left, r->bottom - r->top,
         label, g_sFont, tcol, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}


static void s_save(HWND hwnd) {
    WCHAR v[64];
    GetWindowTextW(g_sEdits[1], v, 64); trim_w(v); if (!valid_ip(v)) { wcscpy(g_sErr, T("err_ip")); InvalidateRect(hwnd, NULL, FALSE); return; }
    GetWindowTextW(g_sEdits[2], v, 64); trim_w(v); if (!valid_ip(v)) { wcscpy(g_sErr, T("err_mask")); InvalidateRect(hwnd, NULL, FALSE); return; }
    GetWindowTextW(g_sEdits[3], v, 64); trim_w(v); if (!valid_ip(v)) { wcscpy(g_sErr, T("err_gw")); InvalidateRect(hwnd, NULL, FALSE); return; }
    GetWindowTextW(g_sEdits[4], v, 64); trim_w(v); if (!valid_ip(v)) { wcscpy(g_sErr, T("err_dns")); InvalidateRect(hwnd, NULL, FALSE); return; }
    GetWindowTextW(g_sEdits[0], v, 64); trim_w(v); if (!v[0]) { wcscpy(g_sErr, T("err_ifc")); InvalidateRect(hwnd, NULL, FALSE); return; }

    GetWindowTextW(g_sEdits[0], g_Interface, 63); trim_w(g_Interface);
    GetWindowTextW(g_sEdits[1], g_StaticIP, 31); trim_w(g_StaticIP);
    GetWindowTextW(g_sEdits[2], g_SubnetMask, 31); trim_w(g_SubnetMask);
    GetWindowTextW(g_sEdits[3], g_Gateway, 31); trim_w(g_Gateway);
    GetWindowTextW(g_sEdits[4], g_DnsServer, 31); trim_w(g_DnsServer);
    save_config_reg();
    logf(C_GREEN, L"[OK] 参数已保存并应用");
    g_sErr[0] = 0;
    s_close(hwnd);
}

/* 后台线程：拉取云端 config.json（多源容错），结果写入 g_sync* 后通知主线程 */
static DWORD WINAPI sync_thread(LPVOID p) {
    (void)p;
    g_syncOk = 0;
    char *raw = fetch_json(REL_CONFIG, "\"interface\"", 3000);
    if (raw) {
        char s_ifc[128] = "", s_ip[32] = "", s_mask[32] = "", s_gw[32] = "", s_dns[32] = "";
        int ok = json_str(raw, "interface", s_ifc, sizeof s_ifc)
              && json_str(raw, "ip", s_ip, sizeof s_ip)
              && json_str(raw, "mask", s_mask, sizeof s_mask)
              && json_str(raw, "gateway", s_gw, sizeof s_gw)
              && json_str(raw, "dns", s_dns, sizeof s_dns);
        WCHAR w_ifc[64], w_ip[32], w_mask[32], w_gw[32], w_dns[32];
        if (ok) {
            MultiByteToWideChar(CP_UTF8, 0, s_ifc, -1, w_ifc, 64);
            MultiByteToWideChar(CP_UTF8, 0, s_ip, -1, w_ip, 32);
            MultiByteToWideChar(CP_UTF8, 0, s_mask, -1, w_mask, 32);
            MultiByteToWideChar(CP_UTF8, 0, s_gw, -1, w_gw, 32);
            MultiByteToWideChar(CP_UTF8, 0, s_dns, -1, w_dns, 32);
            if (!w_ifc[0] || !valid_ip(w_ip) || !valid_ip(w_mask) || !valid_ip(w_gw) || !valid_ip(w_dns))
                ok = 0;
        }
        if (ok) {
            wcscpy(g_syncIfc, w_ifc);  wcscpy(g_syncIP, w_ip);
            wcscpy(g_syncMask, w_mask); wcscpy(g_syncGW, w_gw);
            wcscpy(g_syncDNS, w_dns);
            g_syncOk = 1;
        }
        free(raw);
    }
    PostMessageW(g_mainWnd, WM_APP_SYNC_DONE, 0, 0);
    return 0;
}

/* 设置窗「同步云端配置」：后台拉取，不阻塞界面 */
static void s_sync_cloud(void) {
    if (g_syncBusy) return;
    g_syncBusy = 1;
    wcscpy(g_sErr, T("sync_loading"));
    InvalidateRect(g_settingsWin, NULL, FALSE);
    HWND btn = GetDlgItem(g_settingsWin, IDC_DEFAULT);
    if (btn) EnableWindow(btn, FALSE);
    logf(C_ACCENT, L"正在从云端同步默认配置 ...");
    HANDLE th = CreateThread(NULL, 0, sync_thread, NULL, 0, NULL);
    if (th) CloseHandle(th);
}

static void s_close(HWND hwnd) {
    EnableWindow(GetParent(hwnd), TRUE);
    g_settingsOpen = 0;
    g_settingsWin = NULL;
    DestroyWindow(hwnd);
}

static LRESULT CALLBACK s_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_sFont = make_font(L"Segoe UI", 13, FW_NORMAL);
        g_sBrushPanel = CreateSolidBrush(C_PANEL);
        g_sBrushEdit = CreateSolidBrush(C_CONSOLE);

        int W = SC(440), fx = SC(128), fw = SC(272), fh = SC(30);
        int ys[5] = { SC(70), SC(110), SC(150), SC(190), SC(230) };
        const WCHAR *lbl[5] = { T("lbl_interface"), T("lbl_ip"), T("lbl_mask"), T("lbl_gw"), T("lbl_dns") };
        for (int i = 0; i < 5; i++)
            CreateWindowW(L"STATIC", lbl[i], WS_CHILD | WS_VISIBLE,
                          SC(28), ys[i] + SC(6), SC(92), SC(20), hwnd,
                          (HMENU)(INT_PTR)(IDC_EDIT_NIC + 100 + i), g_hInst, NULL);
        s_make_edit(hwnd, IDC_EDIT_NIC,  fx, ys[0], fw, fh, g_Interface);
        s_make_edit(hwnd, IDC_EDIT_IP,   fx, ys[1], fw, fh, g_StaticIP);
        s_make_edit(hwnd, IDC_EDIT_MASK, fx, ys[2], fw, fh, g_SubnetMask);
        s_make_edit(hwnd, IDC_EDIT_GW,   fx, ys[3], fw, fh, g_Gateway);
        s_make_edit(hwnd, IDC_EDIT_DNS,  fx, ys[4], fw, fh, g_DnsServer);

        s_make_btn(hwnd, IDC_DEFAULT, T("btn_sync"), SC(28), SC(288), SC(120), SC(34));
        s_make_btn(hwnd, IDC_CANCEL,  T("btn_cancel"),     W - SC(104) - SC(104) - SC(16), SC(288), SC(104), SC(34));
        s_make_btn(hwnd, IDC_SAVE,    T("btn_save"),     W - SC(104), SC(288), SC(104), SC(34));
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC h = (HDC)wp;
        SetTextColor(h, C_DIM);
        SetBkColor(h, C_PANEL);
        return (LRESULT)g_sBrushPanel;
    }
    case WM_CTLCOLOREDIT: {
        HDC h = (HDC)wp;
        SetTextColor(h, C_TEXT);
        SetBkColor(h, C_CONSOLE);
        return (LRESULT)g_sBrushEdit;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(mem, bmp);
        fill(mem, 0, 0, rc.right, rc.bottom, C_PANEL);
        text(mem, SC(28), SC(20), SC(300), SC(34), T("settings_title"), g_fTitle, C_ACCENT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        hgradient(mem, SC(28), SC(58), rc.right - SC(56), SC(2), C_ACCENT, C_ACCENT2);
        /* 编辑框底框 */
        int fx = SC(128), fw = SC(272), fh = SC(30);
        int ys[5] = { SC(70), SC(110), SC(150), SC(190), SC(230) };
        for (int i = 0; i < 5; i++)
            rounded(mem, fx - SC(2), ys[i] - SC(2), fw + SC(4), fh + SC(4), SC(6), CLR_NONE, C_GRID, 1);
        if (g_sErr[0]) {
            text(mem, SC(28), SC(258), rc.right - SC(56), SC(20), g_sErr, g_fChip, C_RED, DT_LEFT | DT_SINGLELINE);
        }
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        RECT r = di->rcItem;
        int pressed = (di->itemState & ODS_SELECTED) != 0;
        if (di->CtlID == IDC_SAVE)
            s_draw_btn(di->hDC, &r, T("btn_save"), C_ACCENT, RGB(4, 30, 40), pressed);
        else if (di->CtlID == IDC_CANCEL)
            s_draw_btn(di->hDC, &r, T("btn_cancel"), RGB(28, 38, 58), C_TEXT, pressed);
        else if (di->CtlID == IDC_DEFAULT)
            s_draw_btn(di->hDC, &r, T("btn_sync"), RGB(28, 38, 58), C_ACCENT2, pressed);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == IDC_SAVE) s_save(hwnd);
        else if (LOWORD(wp) == IDC_CANCEL) s_close(hwnd);
        else if (LOWORD(wp) == IDC_DEFAULT) s_sync_cloud();
        return 0;

    case WM_CLOSE:
        s_close(hwnd);
        return 0;

    case WM_DESTROY:
        DeleteObject(g_sFont);
        DeleteObject(g_sBrushPanel);
        DeleteObject(g_sBrushEdit);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void open_settings(HWND parent) {
    if (g_settingsOpen) return;
    g_settingsOpen = 1;
    g_sErr[0] = 0;
    int w = SC(440), h = SC(372);
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + (pr.right - pr.left - w) / 2;
    int y = pr.top + (pr.bottom - pr.top - h) / 2;
    EnableWindow(parent, FALSE);
    HWND sw = CreateWindowW(L"IPSwitchSettingsWin", T("settings_title"),
                            WS_POPUP | WS_CAPTION | WS_SYSMENU,
                            x, y, w, h, parent, NULL, g_hInst, NULL);
    if (!sw) { EnableWindow(parent, TRUE); g_settingsOpen = 0; return; }
    g_settingsWin = sw;
    ShowWindow(sw, SW_SHOW);
    UpdateWindow(sw);
    SetFocus(g_sEdits[0]);
    SendMessageW(g_sEdits[0], EM_SETSEL, 0, -1);
}

/* ============================================================================
 *  在线功能：云端默认配置同步 / 公告弹窗 / 更新下载
 * ==========================================================================*/
/* ------------------------------ 公告弹窗 -------------------------------- */
#define IDN_OK    3001
#define IDN_LATER 3002

static void open_notice(HWND parent, int type);

static void n_draw_btn(HDC hdc, RECT *r, const WCHAR *label,
                       COLORREF fillc, COLORREF tcol, int pressed) {
    rounded(hdc, r->left, r->top, r->right - r->left, r->bottom - r->top, SC(8),
            pressed ? lighten(fillc, 14) : fillc,
            pressed ? lighten(tcol, 40) : C_GRID, 1);
    text(hdc, r->left, r->top, r->right - r->left, r->bottom - r->top,
         label, g_fChip, tcol, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* 更新公告「确定」→ 后台下载新 exe 到程序所在目录 */
static DWORD WINAPI dl_thread(LPVOID p) {
    (void)p;
    WCHAR dir[512];
    get_exe_dir(dir, 512);
    _snwprintf(g_dlPath, 1024, L"%s\\%s", dir, g_ntFile[0] ? g_ntFile : L"netflip-x.exe");
    g_dlPath[1023] = 0;
    int rc = download_file(g_ntUrl, g_dlPath);
    PostMessageW(g_mainWnd, WM_APP_DL_DONE, (WPARAM)rc, 0);
    return 0;
}

static LRESULT CALLBACK n_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        int W = SC(480), by = SC(282);
        HWND b = CreateWindowW(L"BUTTON", g_ntType ? T("btn_ok") : T("btn_gotit"),
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                               W - SC(104) - SC(16) - SC(96), by, SC(96), SC(32),
                               hwnd, (HMENU)(INT_PTR)IDN_OK, g_hInst, NULL);
        SendMessageW(b, WM_SETFONT, (WPARAM)g_fChip, TRUE);
        if (g_ntType == 1) {
            b = CreateWindowW(L"BUTTON", T("btn_later"),
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                              W - SC(104) - SC(16) - SC(96) - SC(10) - SC(96), by,
                              SC(96), SC(32), hwnd, (HMENU)(INT_PTR)IDN_LATER, g_hInst, NULL);
            SendMessageW(b, WM_SETFONT, (WPARAM)g_fChip, TRUE);
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(mem, bmp);
        fill(mem, 0, 0, rc.right, rc.bottom, C_PANEL);
        text(mem, SC(28), SC(18), rc.right - SC(56), SC(34),
             g_ntType ? T("ntc_update_title") : T("ntc_ann_title"),
             g_fTitle, g_ntType ? C_ACCENT : C_ACCENT2,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        hgradient(mem, SC(28), SC(56), rc.right - SC(56), SC(2), C_ACCENT, C_ACCENT2);
        text(mem, SC(28), SC(68), rc.right - SC(56), SC(26), g_ntTitle,
             g_fChip, C_TEXT, DT_LEFT | DT_SINGLELINE);
        /* 内容区：小字号 + 可滚动 */
        int vtop = SC(100);
        int vbot = g_ntType == 1 ? SC(240) : SC(268);
        RECT view = { SC(28), vtop, rc.right - SC(28), vbot };
        int viewH = view.bottom - view.top;
        RECT full = view; full.bottom = 100000;
        HFONT oldF = SelectObject(mem, g_fBody);
        DrawTextW(mem, g_ntContent, -1, &full, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        int contentH = full.bottom - full.top;
        int maxScroll = contentH - viewH;
        if (maxScroll < 0) maxScroll = 0;
        if (g_ntScroll > maxScroll) g_ntScroll = maxScroll;
        if (maxScroll > 0) {   /* 滚动条指示 */
            int sbL = rc.right - SC(16), sbW = SC(4);
            int trackH = viewH - SC(4);
            RECT track = { sbL, view.top + SC(2), sbL + sbW, view.top + SC(2) + trackH };
            fill(mem, track.left, track.top, track.right - track.left, track.bottom - track.top,
                 RGB(28, 38, 58));
            int th = SC(28);
            if (trackH * viewH / contentH > th) th = trackH * viewH / contentH;
            int ty = track.top + (trackH - th) * g_ntScroll / maxScroll;
            fill(mem, sbL, ty, sbW, th, C_ACCENT);
        }
        int saved = SaveDC(mem);
        IntersectClipRect(mem, view.left, view.top, view.right, view.bottom);
        RECT dr = { view.left, view.top - g_ntScroll, view.right, view.top - g_ntScroll + contentH + SC(8) };
        DrawTextW(mem, g_ntContent, -1, &dr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        RestoreDC(mem, saved);
        SelectObject(mem, oldF);
        if (g_ntType == 1)
            text(mem, SC(28), SC(246), rc.right - SC(56), SC(18),
                 T("ntc_dl_hint"), g_fChip, C_DIM, DT_LEFT | DT_SINGLELINE);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        short dz = (short)HIWORD(wp);
        RECT rc; GetClientRect(hwnd, &rc);
        int vtop = SC(100);
        int vbot = g_ntType == 1 ? SC(240) : SC(268);
        RECT view = { SC(28), vtop, rc.right - SC(28), vbot };
        RECT full = view; full.bottom = 100000;
        HDC dc = GetDC(hwnd);
        HFONT oldF = SelectObject(dc, g_fBody);
        DrawTextW(dc, g_ntContent, -1, &full, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        int contentH = full.bottom - full.top;
        SelectObject(dc, oldF); ReleaseDC(hwnd, dc);
        int maxScroll = contentH - (view.bottom - view.top);
        if (maxScroll < 0) maxScroll = 0;
        g_ntScroll -= (dz / WHEEL_DELTA) * SC(28);
        if (g_ntScroll < 0) g_ntScroll = 0;
        if (g_ntScroll > maxScroll) g_ntScroll = maxScroll;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        RECT r = di->rcItem;
        int pressed = (di->itemState & ODS_SELECTED) != 0;
        if (di->CtlID == IDN_OK)
            n_draw_btn(di->hDC, &r, g_ntType ? T("btn_ok") : T("btn_gotit"),
                       C_ACCENT, RGB(4, 30, 40), pressed);
        else if (di->CtlID == IDN_LATER)
            n_draw_btn(di->hDC, &r, T("btn_later"), RGB(28, 38, 58), C_TEXT, pressed);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == IDN_OK) {
            if (g_ntType == 0) {           /* 纯公告：记住已看，仅关闭 */
                g_lastNoticeId = g_ntId;
                save_last_notice_id();
                DestroyWindow(hwnd);
            } else {                       /* 更新公告：后台下载 */
                CreateThread(NULL, 0, dl_thread, NULL, 0, NULL);
                DestroyWindow(hwnd);
            }
        } else if (LOWORD(wp) == IDN_LATER) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_CLOSE:
        if (g_ntType == 0) { g_lastNoticeId = g_ntId; save_last_notice_id(); }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY: {
        g_annShowing = 0;
        HWND p = GetParent(hwnd);
        EnableWindow(p, TRUE);
        if (g_pendingUpdate) { g_pendingUpdate = 0; open_notice(p, 1); }
        else if (g_pendingNotice) { g_pendingNotice = 0; open_notice(p, 0); }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void open_notice(HWND parent, int type) {
    g_ntType = type;
    if (type == 1) {
        wcscpy(g_ntTitle, g_updTitle);
        wcscpy(g_ntContent, g_updContent);
        wcscpy(g_ntUrl, g_updUrl);
        wcscpy(g_ntMd5, g_updMd5);
        wcscpy(g_ntFile, g_updFile);
    } else {
        wcscpy(g_ntTitle, g_ntcTitle);
        wcscpy(g_ntContent, g_ntcContent);
        g_ntId = g_ntcId;
    }
    g_annShowing = 1;
    int w = SC(480), h = SC(336);
    RECT wr = { 0, 0, w, h };
    AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, 0);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + (pr.right - pr.left - ww) / 2;
    int y = pr.top + (pr.bottom - pr.top - wh) / 2;
    EnableWindow(parent, FALSE);
    HWND sw = CreateWindowW(L"IPSwitchNoticeWin", type ? T("ntc_update_title") : T("ntc_ann_title"),
                            WS_POPUP | WS_CAPTION | WS_SYSMENU,
                            x, y, ww, wh, parent, NULL, g_hInst, NULL);
    if (!sw) { EnableWindow(parent, TRUE); g_annShowing = 0; return; }
    ShowWindow(sw, SW_SHOW);
    UpdateWindow(sw);
    SetFocus(sw);
}

/* ------------------------------ 在线检查（后台线程） ------------------------ */
static DWORD WINAPI online_check_thread(LPVOID p) {
    HWND hwnd = (HWND)p;
    logf(C_DIM, L"[在线] 开始检查公告与更新（多源容错）");

    /* 云端界面文本 */
    char *tx = fetch_json(REL_TEXTS, "\"app_title\"", 3000);
    if (tx) {
        apply_texts(tx);
        logf(C_GREEN, L"[在线] 已加载云端界面文本");
        free(tx);
    } else {
        logf(C_DIM, L"[在线] 云端界面文本获取失败，使用内置文本");
    }

    /* 更新检查 */
    char *u = fetch_json(REL_UPDATE, "\"version\"", 3000);
    if (u) {
        char ver[32] = "", title[192] = "", content[1024] = "", url[512] = "", md5[64] = "", file[64] = "";
        if (json_str(u, "version", ver, sizeof ver) && cmp_version(ver, g_version) > 0) {
            logf(C_ACCENT, L"[在线] 发现新版本 %hs（本地 %hs）", ver, g_version);
            json_str(u, "title", title, sizeof title);
            json_str(u, "content", content, sizeof content);
            json_str(u, "url", url, sizeof url);
            json_str(u, "md5", md5, sizeof md5);
            json_str(u, "filename", file, sizeof file);
            unescape_newlines(content);
            MultiByteToWideChar(CP_UTF8, 0, title, -1, g_updTitle, 128);
            MultiByteToWideChar(CP_UTF8, 0, content, -1, g_updContent, 1024);
            MultiByteToWideChar(CP_UTF8, 0, url, -1, g_updUrl, 512);
            MultiByteToWideChar(CP_UTF8, 0, md5, -1, g_updMd5, 64);
            MultiByteToWideChar(CP_UTF8, 0, file, -1, g_updFile, 64);
            g_pendingUpdate = 1;
            PostMessageW(hwnd, WM_APP_UPDATE, 0, 0);
        } else {
            logf(C_DIM, L"[在线] 更新检查：已是最新（云端 %hs）", ver);
        }
        free(u);
    } else {
        logf(C_RED, L"[在线] 更新检查：所有源均无法连接（请检查网络/VPN）");
    }

    /* 公告检查 */
    char *n = fetch_json(REL_NOTICE, "\"notice_id\"", 3000);
    if (n) {
        int nid = json_int(n, "notice_id", 0);
        if (json_bool(n, "show") && nid != g_lastNoticeId) {
            logf(C_ACCENT, L"[在线] 收到新公告 id=%d", nid);
            char title[192] = "", content[1024] = "";
            json_str(n, "title", title, sizeof title);
            json_str(n, "content", content, sizeof content);
            unescape_newlines(content);
            MultiByteToWideChar(CP_UTF8, 0, title, -1, g_ntcTitle, 128);
            MultiByteToWideChar(CP_UTF8, 0, content, -1, g_ntcContent, 1024);
            g_ntcId = nid;
            g_pendingNotice = 1;
            PostMessageW(hwnd, WM_APP_NOTICE, 0, 0);
        } else {
            logf(C_DIM, L"[在线] 公告 id=%d（已看过或未开启，不弹）", nid);
        }
        free(n);
    } else {
        logf(C_RED, L"[在线] 公告获取失败：所有源均无法连接");
    }
    return 0;
}

/* ------------------------------ 主窗口过程 -------------------------------- */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_mainWnd = hwnd;
        g_admin = is_admin();
        g_scale = dpi_scale_of();
        init_fonts();
        layout(hwnd);
        SetTimer(hwnd, 1, 600, NULL);
        log_line(L"系统就绪 · 正在检测当前网络模式", C_DIM);
        mark_current_mode();
        if (!g_admin)
            log_line(L"警告：当前未以管理员身份运行，配置可能失败", C_RED);
        if (detect_current_mode() == 1)
            log_line(L"当前为互联网模式，正在检查公告与更新 ...", C_DIM);
        else
            log_line(L"当前为内网模式，仍尝试在线检查（无网络将提示失败）...", C_DIM);
        /* 每次进入都检查更新：只要云端版本高于本地就弹更新公告 */
        {
            HANDLE th = CreateThread(NULL, 0, online_check_thread, hwnd, 0, NULL);
            if (th) CloseHandle(th);
        }
        return 0;

    case WM_APP_UPDATE:
        if (g_pendingUpdate && !g_annShowing) {
            g_pendingUpdate = 0;
            open_notice(hwnd, 1);
        }
        return 0;

    case WM_APP_NOTICE:
        if (g_pendingNotice && !g_annShowing) {
            g_pendingNotice = 0;
            open_notice(hwnd, 0);
        }
        return 0;

    case WM_APP_DL_DONE: {
        int rc = (int)wp;
        if (rc == 0) {
            logf(C_GREEN, L"[OK] 下载完成：%s", g_dlPath);
            if (g_ntMd5[0]) {
                WCHAR h[40];
                if (md5_file(g_dlPath, h) == 0 && _wcsicmp(h, g_ntMd5) == 0)
                    logf(C_GREEN, L"[OK] MD5 校验通过");
                else
                    logf(C_RED, L"[FAIL] MD5 校验不一致，请勿直接使用");
            }
            MessageBoxW(hwnd, T("msg_upd_done"),
                        T("msg_upd_title"), MB_OK | MB_ICONINFORMATION);
        } else {
            logf(C_RED, L"[FAIL] 下载失败，已为你打开下载链接");
            ShellExecuteW(NULL, L"open", g_ntUrl, NULL, NULL, SW_SHOWNORMAL);
        }
        return 0;
    }

    case WM_APP_SYNC_DONE: {
        g_syncBusy = 0;
        if (g_settingsOpen && g_settingsWin) {
            HWND btn = GetDlgItem(g_settingsWin, IDC_DEFAULT);
            if (btn) EnableWindow(btn, TRUE);
            if (g_syncOk) {
                SetWindowTextW(g_sEdits[0], g_syncIfc);
                SetWindowTextW(g_sEdits[1], g_syncIP);
                SetWindowTextW(g_sEdits[2], g_syncMask);
                SetWindowTextW(g_sEdits[3], g_syncGW);
                SetWindowTextW(g_sEdits[4], g_syncDNS);
                g_sErr[0] = 0;
                logf(C_GREEN, L"[OK] 已同步云端配置到编辑框，保存后生效");
            } else {
                wcscpy(g_sErr, T("sync_fail"));
                logf(C_RED, L"[FAIL] 云端配置同步失败（请检查网络）");
            }
            InvalidateRect(g_settingsWin, NULL, FALSE);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(mem, bmp);
        draw(mem, hwnd);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        g_cursor_on = !g_cursor_on;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hover = -1;
        for (int i = 0; i < 2; i++)
            if (PtInRect(&g_card[i].rc, pt)) hover = i;
        if (PtInRect(&g_refreshRC, pt)) hover = 2;
        if (PtInRect(&g_settingsRC, pt)) hover = 3;
        if (hover != g_hover) {
            g_hover = hover;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        SetCursor(LoadCursor(NULL, hover >= 0 ? IDC_HAND : IDC_ARROW));
        TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        /* 滚动条拖动 */
        if (g_sbDrag) {
            int th = g_sbTrack.bottom - g_sbTrack.top;
            int thumbH = g_sbThumb.bottom - g_sbThumb.top;
            int maxOff = th - thumbH;
            int total = g_log_n, overflow = total - g_maxLines;
            if (overflow > 0 && maxOff > 0) {
                int off = pt.y - g_sbTrack.top - thumbH / 2;
                if (off < 0) off = 0; if (off > maxOff) off = maxOff;
                g_scroll = overflow * off / maxOff;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_hover != -1) { g_hover = -1; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;

    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        if (PtInRect(&g_logPanel, pt)) {
            int total = g_log_n, overflow = total - g_maxLines;
            if (overflow > 0) {
                int delta = GET_WHEEL_DELTA_WPARAM(wp);
                int lines = (delta > 0) ? 3 : -3;  /* 上滚=看更早，下滚=回最新 */
                g_scroll += lines;
                if (g_scroll < 0) g_scroll = 0;
                if (g_scroll > overflow) g_scroll = overflow;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (PtInRect(&g_sbThumb, pt) && g_log_n > g_maxLines) {
            g_sbDrag = 1;
            SetCapture(hwnd);
            return 0;
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (g_sbDrag) { g_sbDrag = 0; ReleaseCapture(); return 0; }
        if (g_busy) return 0;
        if (g_hover == 0) apply_static();
        else if (g_hover == 1) apply_dhcp();
        else if (g_hover == 2) { log_line(L"", C_GRID); refresh_config(); }
        else if (g_hover == 3) open_settings(hwnd);
        if (g_hover >= 0) InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_DPICHANGED: {
        g_scale = HIWORD(wp) / 96.0f;
        destroy_fonts();
        init_fonts();
        RECT *prc = (RECT *)lp;
        SetWindowPos(hwnd, NULL, prc->left, prc->top,
                     prc->right - prc->left, prc->bottom - prc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        destroy_fonts();
        DeleteCriticalSection(&g_logCs);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
    (void)hp; (void)cmd; (void)show;
    if (!is_admin()) {
        MessageBoxW(NULL, T("msg_admin_req"),
                    T("app_title"), MB_OK | MB_ICONWARNING);
        return 1;
    }
    InitializeCriticalSection(&g_logCs);
    g_hInst = hi;
    g_scale = dpi_scale_of();
    load_config_reg();

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(101));
    wc.lpszClassName = L"IPSwitchWin";
    if (!RegisterClassW(&wc)) return 1;

    WNDCLASSW ws = { 0 };
    ws.lpfnWndProc = s_WndProc;
    ws.hInstance = hi;
    ws.hCursor = LoadCursor(NULL, IDC_ARROW);
    ws.hIcon = LoadIcon(hi, MAKEINTRESOURCE(101));
    ws.lpszClassName = L"IPSwitchSettingsWin";
    RegisterClassW(&ws);

    WNDCLASSW wn = { 0 };
    wn.lpfnWndProc = n_WndProc;
    wn.hInstance = hi;
    wn.hCursor = LoadCursor(NULL, IDC_ARROW);
    wn.hIcon = LoadIcon(hi, MAKEINTRESOURCE(101));
    wn.lpszClassName = L"IPSwitchNoticeWin";
    RegisterClassW(&wn);

    int cw = SC(964), chh = SC(620);
    RECT wr = { 0, 0, cw, chh };
    AdjustWindowRectEx(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    int wx = (sx - ww) / 2, wy = (sy - wh) / 2;
    if (wx < 0) wx = 0; if (wy < 0) wy = 0;
    HWND hwnd = CreateWindowW(L"IPSwitchWin", L"上网环境设置工具 v5.3",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              wx, wy, ww, wh,
                              NULL, NULL, hi, NULL);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (g_settingsOpen && IsDialogMessageW(g_settingsWin, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return (int)m.wParam;
}
