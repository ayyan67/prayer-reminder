#include "app.h"
#include <stdio.h>

Config   g_cfg;
Schedule g_sched;

static const wchar_t *ini_path(void)
{
    static wchar_t p[MAX_PATH];
    if (!p[0]) app_data_path(L"config.ini", p, MAX_PATH);
    return p;
}

/* Last-write time of the ini as we last read or wrote it, so the window can
 * notice a hand edit (in Notepad, say) and reload without a restart. */
static int64_t s_iniStamp;

static int64_t ini_stamp(void)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(ini_path(), GetFileExInfoStandard, &fa)) return 0;
    ULARGE_INTEGER u;
    u.LowPart  = fa.ftLastWriteTime.dwLowDateTime;
    u.HighPart = fa.ftLastWriteTime.dwHighDateTime;
    return (int64_t)u.QuadPart;
}

int config_changed_on_disk(void)
{
    int64_t now = ini_stamp();
    return now != 0 && now != s_iniStamp;
}

void config_open_in_editor(void)
{
    /* Make sure the file exists with every key written out, then hand it
     * to whatever opens .ini files (Notepad by default). */
    config_save(&g_cfg);
    HINSTANCE r = ShellExecuteW(NULL, L"open", ini_path(), NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32)
        ShellExecuteW(NULL, L"open", L"notepad.exe", ini_path(), NULL, SW_SHOWNORMAL);
}

/* The masjid this build follows out of the box. Everything here can be
 * overridden in the [masjid] section of config.ini. */
#define DEF_MASJID_NAME L"Muslim Center of New Jersey"
#define DEF_WEBSITE     L"https://mcnjonline.com/"
#define DEF_MASJIDAL_ID L"lJAmBPdR"
#define DEF_ACCENT      RGB(0x9F, 0xB9, 0x3C)

void config_defaults(Config *c)
{
    ZeroMemory(c, sizeof(*c));
    c->lead1On = 1;  c->lead1Min = 20;
    c->lead2On = 1;  c->lead2Min = 5;
    c->atIqamah      = 1;
    c->sound         = 1;
    c->toastSeconds  = 45;
    c->snoozeMinutes = 5;
    c->fridayMode    = FRIDAY_JUMUAH1;
    c->autoStart     = 1;
    c->use24h        = 0;
    c->refreshHour   = 3;
    c->timeShift     = 0;
    c->startMinimized= 0;
    for (int i = 0; i < P_COUNT; i++) {
        c->prayerOn[i]   = 1;
        c->prayerLead[i] = -1;          /* inherit the global lead times */
    }
    c->prayerOn[P_SUNRISE] = 0;         /* informational, not a jama'ah  */
    c->prayerOn[P_JUMUAH2] = 0;         /* most people attend the first  */

    wcscpy_s(c->masjidName, 96,  DEF_MASJID_NAME);
    wcscpy_s(c->website,    256, DEF_WEBSITE);
    wcscpy_s(c->masjidalId, 32,  DEF_MASJIDAL_ID);
    c->accent = DEF_ACCENT;
}

static int geti(const wchar_t *sec, const wchar_t *key, int def)
{
    return (int)GetPrivateProfileIntW(sec, key, def, ini_path());
}
static void seti(const wchar_t *sec, const wchar_t *key, int val)
{
    wchar_t b[32];
    _snwprintf_s(b, 32, _TRUNCATE, L"%d", val);
    WritePrivateProfileStringW(sec, key, b, ini_path());
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void gets_(const wchar_t *sec, const wchar_t *key, wchar_t *out, size_t cch)
{
    wchar_t tmp[512];
    DWORD n = GetPrivateProfileStringW(sec, key, L"", tmp, 512, ini_path());
    /* Trim surrounding whitespace; keep the default when the key is blank. */
    wchar_t *s = tmp, *e = tmp + n;
    while (s < e && (*s == L' ' || *s == L'\t')) s++;
    while (e > s && (e[-1] == L' ' || e[-1] == L'\t' || e[-1] == L'\r')) e--;
    if (e > s) { *e = 0; wcscpy_s(out, cch, s); }
}
static void sets_(const wchar_t *sec, const wchar_t *key, const wchar_t *val)
{
    WritePrivateProfileStringW(sec, key, val, ini_path());
}

/* "9FB93C" or "#9FB93C" -> COLORREF; anything else leaves def untouched. */
static COLORREF parse_hex_colour(const wchar_t *s, COLORREF def)
{
    if (*s == L'#') s++;
    if (wcslen(s) != 6) return def;
    unsigned v = 0;
    for (int i = 0; i < 6; i++) {
        wchar_t c = s[i];
        int d = (c >= L'0' && c <= L'9') ? c - L'0'
              : (c >= L'a' && c <= L'f') ? c - L'a' + 10
              : (c >= L'A' && c <= L'F') ? c - L'A' + 10 : -1;
        if (d < 0) return def;
        v = v * 16 + (unsigned)d;
    }
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

void config_load(Config *c)
{
    config_defaults(c);
    if (GetFileAttributesW(ini_path()) == INVALID_FILE_ATTRIBUTES) {
        config_save(c);
        return;
    }
    c->lead1On       = geti(L"reminders", L"lead1On",       c->lead1On) != 0;
    c->lead1Min      = clampi(geti(L"reminders", L"lead1Min",  c->lead1Min), 1, 180);
    c->lead2On       = geti(L"reminders", L"lead2On",       c->lead2On) != 0;
    c->lead2Min      = clampi(geti(L"reminders", L"lead2Min",  c->lead2Min), 1, 180);
    c->atIqamah      = geti(L"reminders", L"atIqamah",      c->atIqamah) != 0;
    c->sound         = geti(L"reminders", L"sound",         c->sound) != 0;
    c->toastSeconds  = clampi(geti(L"reminders", L"toastSeconds",  c->toastSeconds), 0, 600);
    c->snoozeMinutes = clampi(geti(L"reminders", L"snoozeMinutes", c->snoozeMinutes), 1, 60);
    c->fridayMode    = clampi(geti(L"reminders", L"fridayMode",    c->fridayMode), 0, 3);

    c->autoStart     = geti(L"general", L"autoStart",     c->autoStart) != 0;
    c->use24h        = geti(L"general", L"use24h",        c->use24h) != 0;
    c->refreshHour   = clampi(geti(L"general", L"refreshHour", c->refreshHour), 0, 23);
    c->timeShift     = clampi(geti(L"general", L"timeShift",   c->timeShift), -720, 720);
    c->startMinimized= geti(L"general", L"startMinimized", c->startMinimized) != 0;

    for (int i = 0; i < P_COUNT; i++) {
        wchar_t sec[64];
        _snwprintf_s(sec, 64, _TRUNCATE, L"prayer.%s", kPrayerKey[i]);
        c->prayerOn[i]   = geti(sec, L"enabled", c->prayerOn[i]) != 0;
        c->prayerLead[i] = clampi(geti(sec, L"lead", c->prayerLead[i]), -1, 180);
    }

    wchar_t probe[8] = L"";
    gets_(L"masjid", L"masjidalId", probe, 8);
    gets_(L"masjid", L"name",       c->masjidName, 96);
    gets_(L"masjid", L"website",    c->website,    256);
    gets_(L"masjid", L"masjidalId", c->masjidalId, 32);
    wchar_t hex[32] = L"";
    gets_(L"masjid", L"accent", hex, 32);
    c->accent = parse_hex_colour(hex, c->accent);

    s_iniStamp = ini_stamp();

    /* An ini from before the [masjid] section existed: write the defaults
     * out so anyone opening the file can see what to change. */
    if (!probe[0]) config_save(c);
}

void config_save(const Config *c)
{
    seti(L"reminders", L"lead1On",       c->lead1On);
    seti(L"reminders", L"lead1Min",      c->lead1Min);
    seti(L"reminders", L"lead2On",       c->lead2On);
    seti(L"reminders", L"lead2Min",      c->lead2Min);
    seti(L"reminders", L"atIqamah",      c->atIqamah);
    seti(L"reminders", L"sound",         c->sound);
    seti(L"reminders", L"toastSeconds",  c->toastSeconds);
    seti(L"reminders", L"snoozeMinutes", c->snoozeMinutes);
    seti(L"reminders", L"fridayMode",    c->fridayMode);

    seti(L"general", L"autoStart",      c->autoStart);
    seti(L"general", L"use24h",         c->use24h);
    seti(L"general", L"refreshHour",    c->refreshHour);
    seti(L"general", L"timeShift",      c->timeShift);
    seti(L"general", L"startMinimized", c->startMinimized);

    for (int i = 0; i < P_COUNT; i++) {
        wchar_t sec[64];
        _snwprintf_s(sec, 64, _TRUNCATE, L"prayer.%s", kPrayerKey[i]);
        seti(sec, L"enabled", c->prayerOn[i]);
        seti(sec, L"lead",    c->prayerLead[i]);
    }

    sets_(L"masjid", L"name",       c->masjidName);
    sets_(L"masjid", L"website",    c->website);
    sets_(L"masjid", L"masjidalId", c->masjidalId);
    wchar_t hex[16];
    _snwprintf_s(hex, 16, _TRUNCATE, L"%02X%02X%02X",
                 GetRValue(c->accent), GetGValue(c->accent), GetBValue(c->accent));
    sets_(L"masjid", L"accent", hex);

    /* Windows buffers profile writes; push them to disk now so the file
     * matches what the app is using even if we are killed. */
    WritePrivateProfileStringW(NULL, NULL, NULL, ini_path());
    s_iniStamp = ini_stamp();
}

void config_apply_autostart(const Config *c)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    if (c->autoStart) {
        wchar_t exe[MAX_PATH], val[MAX_PATH + 16];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        _snwprintf_s(val, MAX_PATH + 16, _TRUNCATE, L"\"%s\" --tray", exe);
        RegSetValueExW(k, APP_NAME, 0, REG_SZ, (const BYTE *)val,
                       (DWORD)((wcslen(val) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, APP_NAME);
    }
    RegCloseKey(k);
}
