#include "app.h"
#include <stdio.h>
#include <time.h>
#include <shlobj.h>

const wchar_t *kPrayerName[P_COUNT] = {
    L"Fajr", L"Sunrise", L"Dhuhr", L"Asr", L"Maghrib", L"Isha",
    L"Jumu'ah 1", L"Jumu'ah 2"
};
const wchar_t *kPrayerKey[P_COUNT] = {
    L"fajr", L"sunrise", L"dhuhr", L"asr", L"maghrib", L"isha",
    L"jumuah1", L"jumuah2"
};

int64_t local_now(void) { return (int64_t)time(NULL); }

void local_ymd(int *y, int *m, int *d, int *dow)
{
    time_t t = time(NULL);
    struct tm lt;
    localtime_s(&lt, &t);
    if (y)   *y   = lt.tm_year + 1900;
    if (m)   *m   = lt.tm_mon + 1;
    if (d)   *d   = lt.tm_mday;
    if (dow) *dow = lt.tm_wday;
}

int64_t ymd_hm_to_unix(int y, int m, int d, int minutes)
{
    struct tm lt = {0};
    lt.tm_year  = y - 1900;
    lt.tm_mon   = m - 1;
    lt.tm_mday  = d;
    lt.tm_hour  = minutes / 60;
    lt.tm_min   = minutes % 60;
    lt.tm_sec   = 0;
    lt.tm_isdst = -1;              /* let the CRT resolve DST */
    return (int64_t)mktime(&lt);
}

void fmt_time(int minutes, wchar_t *out, size_t cch)
{
    if (minutes < 0) { wcscpy_s(out, cch, L"--:--"); return; }
    int h = (minutes / 60) % 24, mi = minutes % 60;
    if (g_cfg.use24h) {
        _snwprintf_s(out, cch, _TRUNCATE, L"%02d:%02d", h, mi);
    } else {
        const wchar_t *ap = h < 12 ? L"AM" : L"PM";
        int h12 = h % 12; if (h12 == 0) h12 = 12;
        _snwprintf_s(out, cch, _TRUNCATE, L"%d:%02d %s", h12, mi, ap);
    }
}

void fmt_countdown(int64_t secs, wchar_t *out, size_t cch)
{
    if (secs < 0) secs = 0;
    int64_t m = secs / 60, h = m / 60;
    if (h > 0)      _snwprintf_s(out, cch, _TRUNCATE, L"%lldh %lldm", h, m % 60);
    else if (m > 0) _snwprintf_s(out, cch, _TRUNCATE, L"%lldm", m);
    else            _snwprintf_s(out, cch, _TRUNCATE, L"%llds", secs);
}

void play_alert(void)
{
    if (!g_cfg.sound) return;
    /* Prefer the user's notification sound; fall back to a system beep. */
    if (!PlaySoundW(L"Notification.Reminder", NULL,
                    SND_ALIAS | SND_ASYNC | SND_NODEFAULT))
        MessageBeep(MB_ICONASTERISK);
}

void open_url(const wchar_t *url)
{
    ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
}

wchar_t *app_data_path(const wchar_t *leaf, wchar_t *buf, size_t cch)
{
    wchar_t base[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, base)))
        return NULL;
    _snwprintf_s(buf, cch, _TRUNCATE, L"%s\\PrayerReminder", base);
    CreateDirectoryW(buf, NULL);
    if (leaf && *leaf)
        _snwprintf_s(buf, cch, _TRUNCATE, L"%s\\PrayerReminder\\%s", base, leaf);
    return buf;
}
