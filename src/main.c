#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>

HINSTANCE g_inst;

#define CTRL_CLASS L"PrayerReminderCtrl"
#define TRAY_UID   1
#define T_TICK     1

/* menu ids */
enum {
    M_OPEN = 1000, M_REFRESH, M_SETTINGS, M_SITE, M_EXIT,
    M_PAUSE_1H, M_PAUSE_4H, M_PAUSE_TOMORROW, M_PAUSE_OFF
};

static HWND    s_ctrl;
static HICON   s_trayIcon;
static UINT    s_taskbarCreated;
static int64_t s_lastCheck;
static int64_t s_pauseUntil;
static int     s_lastRefreshDay = -1;
static wchar_t s_tip[128];

/* ---- tray ------------------------------------------------------------- */

static void tray_add(void)
{
    NOTIFYICONDATAW ni;
    ZeroMemory(&ni, sizeof ni);
    ni.cbSize           = sizeof ni;
    ni.hWnd             = s_ctrl;
    ni.uID              = TRAY_UID;
    ni.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    ni.uCallbackMessage = WM_APP_TRAY;
    ni.hIcon            = s_trayIcon;
    wcscpy_s(ni.szTip, 128, APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &ni);
}

static void tray_update(const wchar_t *tip)
{
    NOTIFYICONDATAW ni;
    ZeroMemory(&ni, sizeof ni);
    ni.cbSize = sizeof ni;
    ni.hWnd   = s_ctrl;
    ni.uID    = TRAY_UID;
    ni.uFlags = NIF_TIP;
    wcscpy_s(ni.szTip, 128, tip);
    Shell_NotifyIconW(NIM_MODIFY, &ni);
}

static void tray_remove(void)
{
    NOTIFYICONDATAW ni;
    ZeroMemory(&ni, sizeof ni);
    ni.cbSize = sizeof ni;
    ni.hWnd   = s_ctrl;
    ni.uID    = TRAY_UID;
    Shell_NotifyIconW(NIM_DELETE, &ni);
}

static void build_tip(void)
{
    Event ev;
    wchar_t tip[128];

    if (s_pauseUntil > local_now()) {
        wcscpy_s(tip, 128, L"Prayer Reminder \x2014 paused");
    } else if (g_sched.count > 0 && sched_next(&ev)) {
        wchar_t t[32], cd[32];
        fmt_time(ev.iqamahMin, t, 32);
        int64_t target = ymd_hm_to_unix(ev.y, ev.m, ev.d, ev.iqamahMin);
        fmt_countdown(target - local_now(), cd, 32);
        _snwprintf_s(tip, 128, _TRUNCATE, L"%s %s \x2014 in %s",
                     kPrayerName[ev.prayer], t, cd);
    } else if (g_fetchState == 1) {
        wcscpy_s(tip, 128, L"Prayer Reminder \x2014 fetching times\x2026");
    } else {
        wcscpy_s(tip, 128, L"Prayer Reminder \x2014 no times yet");
    }

    if (wcscmp(tip, s_tip) != 0) {
        wcscpy_s(s_tip, 128, tip);
        tray_update(tip);
    }
}

static void show_menu(void)
{
    HMENU m = CreatePopupMenu();
    HMENU pause = CreatePopupMenu();

    Event ev;
    if (g_sched.count > 0 && sched_next(&ev)) {
        wchar_t t[32], head[128];
        fmt_time(ev.iqamahMin, t, 32);
        _snwprintf_s(head, 128, _TRUNCATE, L"Next:  %s  %s", kPrayerName[ev.prayer], t);
        AppendMenuW(m, MF_STRING | MF_DISABLED, 0, head);
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    }

    AppendMenuW(m, MF_STRING, M_OPEN,     L"Open");
    AppendMenuW(m, MF_STRING, M_SETTINGS, L"Settings");
    AppendMenuW(m, MF_STRING, M_REFRESH,  L"Refresh times now");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);

    int paused = s_pauseUntil > local_now();
    AppendMenuW(pause, MF_STRING, M_PAUSE_1H,       L"For 1 hour");
    AppendMenuW(pause, MF_STRING, M_PAUSE_4H,       L"For 4 hours");
    AppendMenuW(pause, MF_STRING, M_PAUSE_TOMORROW, L"Until tomorrow");
    if (paused) {
        AppendMenuW(pause, MF_SEPARATOR, 0, NULL);
        AppendMenuW(pause, MF_STRING, M_PAUSE_OFF, L"Resume reminders");
    }
    AppendMenuW(m, MF_POPUP | (paused ? MF_CHECKED : 0), (UINT_PTR)pause,
                paused ? L"Reminders paused" : L"Pause reminders");

    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, M_SITE, L"Open masjid website");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, M_EXIT, L"Exit");

    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(s_ctrl);          /* so the menu dismisses properly */
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, s_ctrl, NULL);
    PostMessageW(s_ctrl, WM_NULL, 0, 0);

    DestroyMenu(pause);
    DestroyMenu(m);
}

/* ---- firing ----------------------------------------------------------- */

static void check_events(void)
{
    int64_t now = local_now();

    if (s_lastCheck == 0) { s_lastCheck = now; return; }
    if (now < s_lastCheck) s_lastCheck = now;      /* clock moved backwards */

    if (s_pauseUntil > now) { s_lastCheck = now; return; }

    Event ev[128];
    int n = sched_build(ev, 128);
    for (int i = 0; i < n; i++) {
        if (ev[i].when <= s_lastCheck || ev[i].when > now) continue;
        /* After a long sleep or a suspend, skip anything well past due
         * instead of firing a burst of stale reminders. */
        if (now - ev[i].when > 300) continue;
        /* Show the earliest one now and leave the rest pending, so a second
         * event in the same second is delayed by a tick rather than lost. */
        toast_show(&ev[i]);
        s_lastCheck = ev[i].when;
        return;
    }
    s_lastCheck = now;
}

static void maybe_daily_refresh(void)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (st.wHour != g_cfg.refreshHour) return;
    if (s_lastRefreshDay == st.wDay) return;
    s_lastRefreshDay = st.wDay;
    times_fetch_async(s_ctrl);
}

/* Fetch again if the cache does not cover today or is getting stale. */
static void ensure_fresh(void)
{
    if (g_fetchState == 1) return;
    if (!times_today(&g_sched)) { times_fetch_async(s_ctrl); return; }
    if (local_now() - g_sched.fetchedAt > 24 * 3600) times_fetch_async(s_ctrl);
}

/* ---- controller window ------------------------------------------------ */

static LRESULT CALLBACK ctrl_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == s_taskbarCreated && s_taskbarCreated) {
        tray_add();
        s_tip[0] = 0;
        build_tip();
        return 0;
    }

    switch (msg) {
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP:   mainwnd_show(); return 0;
        case WM_LBUTTONDBLCLK: mainwnd_show(); return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: show_menu(); return 0;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case M_OPEN:     mainwnd_show(); break;
        case M_SETTINGS: mainwnd_goto_settings(); break;
        case M_REFRESH:  times_fetch_async(hwnd); mainwnd_refresh(); break;
        case M_SITE:     open_url(g_cfg.website); break;
        case M_PAUSE_1H: s_pauseUntil = local_now() + 3600; break;
        case M_PAUSE_4H: s_pauseUntil = local_now() + 4 * 3600; break;
        case M_PAUSE_TOMORROW: {
            int y, m, d, dow;
            local_ymd(&y, &m, &d, &dow);
            s_pauseUntil = ymd_hm_to_unix(y, m, d, 0) + 24 * 3600;
            break;
        }
        case M_PAUSE_OFF: s_pauseUntil = 0; break;
        case M_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        s_tip[0] = 0;
        build_tip();
        return 0;

    case WM_APP_FETCHED: {
        if (wp == 1 && lp) {
            Schedule *s = (Schedule *)lp;
            g_sched = *s;
            free(s);
            g_fetchState = 0;
        } else {
            g_fetchState = 2;
        }
        mainwnd_refresh();
        s_tip[0] = 0;
        build_tip();
        return 0;
    }

    case WM_TIMER:
        if (wp == T_TICK) {
            check_events();
            maybe_daily_refresh();
            /* Self-heal hourly: if the daily refresh was missed because the
             * machine was asleep, or the cache no longer covers today. */
            {
                SYSTEMTIME st;
                GetLocalTime(&st);
                if (st.wMinute == 0 && st.wSecond < 2) ensure_fresh();
            }
            build_tip();
            if (g_mainWnd && IsWindowVisible(g_mainWnd))
                InvalidateRect(g_mainWnd, NULL, FALSE);
        }
        return 0;

    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
            s_lastCheck = local_now();      /* do not replay missed alerts */
            ensure_fresh();
        }
        return TRUE;

    case WM_APP_SHOWMAIN:
        mainwnd_show();
        return 0;

    case WM_DESTROY:
        tray_remove();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- entry ------------------------------------------------------------ */

static void enable_dpi_awareness(void)
{
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (!u) return;
    typedef BOOL (WINAPI *PFN)(HANDLE);
    PFN set = (PFN)(void *)GetProcAddress(u, "SetProcessDpiAwarenessContext");
    if (set) set((HANDLE)-4);   /* PER_MONITOR_AWARE_V2 */
    else SetProcessDPIAware();
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)prev; (void)show;

    HANDLE mtx = CreateMutexW(NULL, TRUE, APP_MUTEX);
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(CTRL_CLASS, NULL);
        if (other) PostMessageW(other, WM_APP_SHOWMAIN, 0, 0);
        return 0;
    }

    g_inst = hi;
    enable_dpi_awareness();
    config_load(&g_cfg);
    config_apply_autostart(&g_cfg);

    s_trayIcon = gfx_make_icon(GetSystemMetrics(SM_CXSMICON));

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.cbSize        = sizeof wc;
    wc.lpfnWndProc   = ctrl_proc;
    wc.hInstance     = hi;
    wc.lpszClassName = CTRL_CLASS;
    RegisterClassExW(&wc);

    s_ctrl = CreateWindowExW(0, CTRL_CLASS, L"", 0, 0, 0, 0, 0,
                             NULL, NULL, hi, NULL);
    if (!s_ctrl) return 1;

    s_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    mainwnd_register(hi);
    toast_register(hi);
    g_mainWnd = mainwnd_create(hi);

    times_load_cache(&g_sched);          /* show something immediately */
    mainwnd_refresh();

    tray_add();
    build_tip();

    s_lastCheck = local_now();
    SetTimer(s_ctrl, T_TICK, 1000, NULL);
    times_fetch_async(s_ctrl);

    int startHidden = g_cfg.startMinimized || (cmd && wcsstr(cmd, L"--tray"));
    if (!startHidden) mainwnd_show();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    tray_remove();
    if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); }
    return 0;
}
