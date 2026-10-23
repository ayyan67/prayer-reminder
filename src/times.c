#include "app.h"
#include <wctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ===================== tiny JSON helpers ==============================
 * The payload shape is fixed, so a tolerant scanner beats a real parser. */

static const char *skip_ws(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

/* Walk from '{' (or '[') to its matching close, respecting strings. */
static const char *match_brace(const char *p, const char *end)
{
    char open = *p, close = (open == '{') ? '}' : ']';
    int depth = 0, instr = 0;
    for (; p < end; p++) {
        if (instr) {
            if (*p == '\\') p++;
            else if (*p == '"') instr = 0;
            continue;
        }
        if (*p == '"') instr = 1;
        else if (*p == open) depth++;
        else if (*p == close && --depth == 0) return p;
    }
    return NULL;
}

/* Find "key" : "value" inside [obj,end). Returns 1 on success. */
static int json_str(const char *obj, const char *end,
                    const char *key, char *out, int cch)
{
    char pat[64];
    int  plen = _snprintf_s(pat, sizeof pat, _TRUNCATE, "\"%s\"", key);
    for (const char *p = obj; p + plen < end; p++) {
        if (memcmp(p, pat, plen) != 0) continue;
        const char *q = skip_ws(p + plen, end);
        if (q >= end || *q != ':') continue;
        q = skip_ws(q + 1, end);
        if (q >= end || *q != '"') continue;
        q++;
        int n = 0;
        while (q < end && *q != '"' && n < cch - 1) {
            if (*q == '\\' && q + 1 < end) q++;
            out[n++] = *q++;
        }
        out[n] = 0;
        return 1;
    }
    return 0;
}

/* Locate the array that follows "key": and hand back its interior. */
static int json_array(const char *doc, const char *end, const char *key,
                      const char **arrBeg, const char **arrEnd)
{
    char pat[64];
    int  plen = _snprintf_s(pat, sizeof pat, _TRUNCATE, "\"%s\"", key);
    for (const char *p = doc; p + plen < end; p++) {
        if (memcmp(p, pat, plen) != 0) continue;
        const char *q = skip_ws(p + plen, end);
        if (q >= end || *q != ':') continue;
        q = skip_ws(q + 1, end);
        if (q >= end || *q != '[') continue;
        const char *close = match_brace(q, end);
        if (!close) return 0;
        *arrBeg = q + 1;
        *arrEnd = close;
        return 1;
    }
    return 0;
}

/* ===================== value parsing ================================== */

/* "5:24AM" / "1:15 PM" / "12:52PM" / "1:35" -> minutes from midnight. */
static int parse_clock(const char *s, int assumePM)
{
    if (!s || !*s) return NO_TIME;
    while (*s == ' ') s++;
    if (*s < '0' || *s > '9') return NO_TIME;

    int h = 0, m = 0;
    while (*s >= '0' && *s <= '9') h = h * 10 + (*s++ - '0');
    if (*s != ':') return NO_TIME;
    s++;
    if (*s < '0' || *s > '9') return NO_TIME;
    while (*s >= '0' && *s <= '9') m = m * 10 + (*s++ - '0');
    if (h > 23 || m > 59) return NO_TIME;

    while (*s == ' ' || *s == '.') s++;
    int pm = -1;
    if (*s == 'a' || *s == 'A') pm = 0;
    else if (*s == 'p' || *s == 'P') pm = 1;

    if (pm < 0) pm = assumePM;          /* the page omits it on Jumu'ah rows */
    if (pm == 1 && h < 12) h += 12;
    if (pm == 0 && h == 12) h = 0;
    return h * 60 + m;
}

/* "Thursday, Sep 17, 2026" -> y/m/d. Returns 1 on success. */
static int parse_date(const char *s, int *y, int *m, int *d)
{
    static const char *mon[12] = { "Jan","Feb","Mar","Apr","May","Jun",
                                   "Jul","Aug","Sep","Oct","Nov","Dec" };
    const char *c = strchr(s, ',');
    c = c ? c + 1 : s;
    while (*c == ' ') c++;

    int mi = -1;
    for (int i = 0; i < 12; i++)
        if (_strnicmp(c, mon[i], 3) == 0) { mi = i; break; }
    if (mi < 0) return 0;
    while (*c && *c != ' ') c++;
    while (*c == ' ') c++;

    int dd = atoi(c);
    const char *c2 = strchr(c, ',');
    if (!c2) return 0;
    int yy = atoi(c2 + 1);
    if (dd < 1 || dd > 31 || yy < 2000 || yy > 2100) return 0;

    *y = yy; *m = mi + 1; *d = dd;
    return 1;
}

static int dow_of(int y, int m, int d)
{
    struct tm lt;
    ZeroMemory(&lt, sizeof lt);
    lt.tm_year = y - 1900; lt.tm_mon = m - 1; lt.tm_mday = d;
    lt.tm_hour = 12; lt.tm_isdst = -1;
    mktime(&lt);
    return lt.tm_wday;
}

/* ===================== schedule assembly ============================== */

static void day_init(DayTimes *dt)
{
    ZeroMemory(dt, sizeof(*dt));
    for (int i = 0; i < P_COUNT; i++) { dt->adhan[i] = NO_TIME; dt->iqamah[i] = NO_TIME; }
}

static DayTimes *day_for(Schedule *s, int y, int m, int d)
{
    for (int i = 0; i < s->count; i++)
        if (s->days[i].y == y && s->days[i].m == m && s->days[i].d == d)
            return &s->days[i];
    if (s->count >= MAX_DAYS) return NULL;
    DayTimes *dt = &s->days[s->count++];
    day_init(dt);
    dt->y = y; dt->m = m; dt->d = d; dt->dow = dow_of(y, m, d);
    return dt;
}

int times_parse_json(const char *json, Schedule *out)
{
    if (!json) return 0;
    const char *end = json + strlen(json);
    const char *ab, *ae;
    char v[64];

    Schedule s;
    ZeroMemory(&s, sizeof s);

    /* --- athan times ------------------------------------------------- */
    if (json_array(json, end, "salah", &ab, &ae)) {
        for (const char *p = ab; p < ae; ) {
            p = skip_ws(p, ae);
            if (p >= ae || *p != '{') break;
            const char *oe = match_brace(p, ae);
            if (!oe) break;

            int y, m, d;
            if (json_str(p, oe, "date", v, sizeof v) && parse_date(v, &y, &m, &d)) {
                DayTimes *dt = day_for(&s, y, m, d);
                if (dt) {
                    struct { const char *k; int p; int pm; } f[6] = {
                        { "fajr",    P_FAJR,    0 }, { "sunrise", P_SUNRISE, 0 },
                        { "zuhr",    P_DHUHR,   1 }, { "asr",     P_ASR,     1 },
                        { "maghrib", P_MAGHRIB, 1 }, { "isha",    P_ISHA,    1 },
                    };
                    for (int i = 0; i < 6; i++)
                        if (json_str(p, oe, f[i].k, v, sizeof v))
                            dt->adhan[f[i].p] = parse_clock(v, f[i].pm);

                    char hd[32], hm[48];
                    if (json_str(p, oe, "hijri_date", hd, sizeof hd) &&
                        json_str(p, oe, "hijri_month", hm, sizeof hm)) {
                        /* "6, 1448" + "Rabi Al-Thani" -> "6 Rabi Al-Thani 1448" */
                        char *comma = strchr(hd, ',');
                        int hday  = atoi(hd);
                        int hyear = comma ? atoi(comma + 1) : 0;
                        wchar_t wmon[48];
                        MultiByteToWideChar(CP_UTF8, 0, hm, -1, wmon, 48);
                        _snwprintf_s(dt->hijri, 64, _TRUNCATE, L"%d %s %d",
                                     hday, wmon, hyear);
                    }
                }
            }
            p = oe + 1;
            const char *c = skip_ws(p, ae);
            if (c < ae && *c == ',') p = c + 1;
        }
    }

    /* --- iqamah times ------------------------------------------------ */
    if (json_array(json, end, "iqamah", &ab, &ae)) {
        for (const char *p = ab; p < ae; ) {
            p = skip_ws(p, ae);
            if (p >= ae || *p != '{') break;
            const char *oe = match_brace(p, ae);
            if (!oe) break;

            int y, m, d;
            if (json_str(p, oe, "date", v, sizeof v) && parse_date(v, &y, &m, &d)) {
                DayTimes *dt = day_for(&s, y, m, d);
                if (dt) {
                    struct { const char *k; int p; int pm; } f[7] = {
                        { "fajr",    P_FAJR,    0 }, { "zuhr",    P_DHUHR,   1 },
                        { "asr",     P_ASR,     1 }, { "maghrib", P_MAGHRIB, 1 },
                        { "isha",    P_ISHA,    1 },
                        /* jummah1/2 carry the khutbah start time */
                        { "jummah1", P_JUMUAH1, 1 }, { "jummah2", P_JUMUAH2, 1 },
                    };
                    for (int i = 0; i < 7; i++)
                        if (json_str(p, oe, f[i].k, v, sizeof v))
                            dt->iqamah[f[i].p] = parse_clock(v, f[i].pm);
                }
            }
            p = oe + 1;
            const char *c = skip_ws(p, ae);
            if (c < ae && *c == ',') p = c + 1;
        }
    }

    if (s.count == 0) return 0;
    s.fetchedAt = local_now();
    *out = s;
    return 1;
}

/* ===================== HTML fallback ==================================
 * If the JSON feed ever goes away, read today's numbers off the homepage. */

static const char *ci_find(const char *hay, const char *end, const char *needle)
{
    size_t n = strlen(needle);
    for (const char *p = hay; p + n <= end; p++)
        if (_strnicmp(p, needle, (int)n) == 0) return p;
    return NULL;
}

static int html_time_after(const char *p, const char *end,
                           const char *label, int window, int assumePM)
{
    const char *lim = (p + window < end) ? p + window : end;
    const char *l = ci_find(p, lim, label);
    if (!l) return NO_TIME;
    l += strlen(label);

    /* Skip the separator junk between the label and the digits, including
     * any markup the page injects mid-line. */
    const char *q = l;
    while (q < lim && (*q < '0' || *q > '9')) {
        if (*q == '<') { while (q < lim && *q != '>') q++; }
        q++;
    }
    if (q >= lim) return NO_TIME;
    return parse_clock(q, assumePM);
}

static int times_parse_html(const char *html, Schedule *out)
{
    if (!html) return 0;
    const char *end = html + strlen(html);

    Schedule s;
    ZeroMemory(&s, sizeof s);
    int y, m, d, dow;
    local_ymd(&y, &m, &d, &dow);
    DayTimes *dt = day_for(&s, y, m, d);
    if (!dt) return 0;

    struct { const char *label; int p; int pm; } rows[5] = {
        { "Fajr",    P_FAJR,    0 }, { "Dhuhr",   P_DHUHR,   1 },
        { "Asr",     P_ASR,     1 }, { "Maghrib", P_MAGHRIB, 1 },
        { "Isha",    P_ISHA,    1 },
    };
    int found = 0;
    for (int i = 0; i < 5; i++) {
        const char *at = ci_find(html, end, rows[i].label);
        if (!at) continue;
        dt->adhan[rows[i].p]  = html_time_after(at, end, "Adhan",  1200, rows[i].pm);
        dt->iqamah[rows[i].p] = html_time_after(at, end, "Iqamah", 1200, rows[i].pm);
        if (dt->iqamah[rows[i].p] != NO_TIME) found++;
    }

    const char *sr = ci_find(html, end, "Sunrise");
    if (sr) dt->adhan[P_SUNRISE] = html_time_after(sr, end, "Sunrise", 1200, 0);

    const char *j1 = ci_find(html, end, "Jumah 1st");
    if (j1) dt->iqamah[P_JUMUAH1] = html_time_after(j1, end, "Khutbah", 1200, 1);
    const char *j2 = ci_find(html, end, "Jumah 2nd");
    if (j2) dt->iqamah[P_JUMUAH2] = html_time_after(j2, end, "Khutbah", 1200, 1);

    if (found < 3) return 0;          /* partial scrape is worse than none */
    s.fetchedAt = local_now();
    *out = s;
    return 1;
}

/* ===================== cache ========================================== */

/* One cache per masjid id, so switching masjids in config.ini can never
 * show the previous masjid's times. Recomputed on every call because the
 * id can change at runtime when the ini is reloaded. */
static const wchar_t *cache_path(void)
{
    static wchar_t p[MAX_PATH];
    wchar_t leaf[64] = L"cache-";
    size_t  n = wcslen(leaf);
    for (const wchar_t *s = g_cfg.masjidalId; *s && n < 48; s++)
        if (iswalnum(*s)) leaf[n++] = *s;
    leaf[n] = 0;
    wcscat_s(leaf, 64, L".json");
    app_data_path(leaf, p, MAX_PATH);
    return p;
}

/* "https://example.org/prayer-times" -> "example.org" (for the HTML fallback). */
static int website_host(wchar_t *out, size_t cch)
{
    const wchar_t *s = g_cfg.website;
    if (!wcsncmp(s, L"https://", 8)) s += 8;
    else if (!wcsncmp(s, L"http://", 7)) s += 7;
    size_t n = 0;
    while (s[n] && s[n] != L'/' && s[n] != L'?' && n + 1 < cch) { out[n] = s[n]; n++; }
    out[n] = 0;
    return n > 0;
}

void times_save_cache(const char *json, int len)
{
    HANDLE h = CreateFileW(cache_path(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(h, json, (DWORD)len, &w, NULL);
    CloseHandle(h);
}

int times_load_cache(Schedule *out)
{
    HANDLE h = CreateFileW(cache_path(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD sz = GetFileSize(h, NULL), got = 0;
    if (sz == INVALID_FILE_SIZE || sz == 0 || sz > 8u * 1024 * 1024) {
        CloseHandle(h);
        return 0;
    }
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { CloseHandle(h); return 0; }
    ReadFile(h, buf, sz, &got, NULL);
    buf[got] = 0;
    CloseHandle(h);

    int ok = times_parse_json(buf, out);
    if (ok) {
        /* Report when the data was really fetched, not when we reread it. */
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(cache_path(), GetFileExInfoStandard, &fa)) {
            ULARGE_INTEGER u;
            u.LowPart  = fa.ftLastWriteTime.dwLowDateTime;
            u.HighPart = fa.ftLastWriteTime.dwHighDateTime;
            out->fetchedAt = (int64_t)(u.QuadPart / 10000000ULL) - 11644473600LL;
        }
    }
    free(buf);
    return ok;
}

/* ===================== fetch ========================================== */

static DWORD WINAPI fetch_thread(LPVOID param)
{
    HWND notify = (HWND)param;
    int  y, m, d, dow;
    local_ymd(&y, &m, &d, &dow);

    /* Two weeks ahead: enough to ride out an outage or a spell offline. */
    struct tm lt;
    ZeroMemory(&lt, sizeof lt);
    lt.tm_year = y - 1900; lt.tm_mon = m - 1; lt.tm_mday = d + 13;
    lt.tm_hour = 12; lt.tm_isdst = -1;
    mktime(&lt);

    wchar_t path[256];
    _snwprintf_s(path, 256, _TRUNCATE,
        L"/api/v1/time/range?masjid_id=%s&from_date=%04d-%02d-%02d&to_date=%04d-%02d-%02d",
        g_cfg.masjidalId, y, m, d, lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);

    int   len = 0, ok = 0;
    char *body = net_get(L"masjidal.com", path, &len);
    Schedule s;

    if (body && times_parse_json(body, &s)) {
        times_save_cache(body, len);
        ok = 1;
    }
    if (body) { free(body); body = NULL; }

    if (!ok) {                       /* fall back to the masjid homepage */
        wchar_t host[128];
        if (website_host(host, 128)) {
            body = net_get(host, L"/", &len);
            if (body && times_parse_html(body, &s)) ok = 1;
            if (body) free(body);
        }
    }

    if (ok) {
        Schedule *heap = (Schedule *)malloc(sizeof(Schedule));
        if (heap) {
            *heap = s;
            PostMessageW(notify, WM_APP_FETCHED, 1, (LPARAM)heap);
            return 0;
        }
    }
    PostMessageW(notify, WM_APP_FETCHED, 0, 0);
    return 0;
}

void times_fetch_async(HWND notify)
{
    if (g_fetchState == 1) return;         /* one in flight is enough */
    g_fetchState = 1;
    HANDLE t = CreateThread(NULL, 0, fetch_thread, (LPVOID)notify, 0, NULL);
    if (t) CloseHandle(t);
    else   g_fetchState = 2;
}

/* ===================== lookups ======================================== */

const DayTimes *times_for_date(const Schedule *s, int y, int m, int d)
{
    for (int i = 0; i < s->count; i++)
        if (s->days[i].y == y && s->days[i].m == m && s->days[i].d == d)
            return &s->days[i];
    return NULL;
}

const DayTimes *times_today(const Schedule *s)
{
    int y, m, d, dow;
    local_ymd(&y, &m, &d, &dow);
    return times_for_date(s, y, m, d);
}
