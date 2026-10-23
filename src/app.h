/* PrayerReminder - iqamah reminders for your masjid, from its Masjidal feed.
 * Shared declarations. */
#ifndef APP_H
#define APP_H

#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <stdint.h>

#define APP_NAME     L"Prayer Reminder"
#define APP_CLASS    L"PrayerReminderMainWnd"
#define TOAST_CLASS  L"PrayerReminderToastWnd"
#define APP_MUTEX    L"PrayerReminder.SingleInstance.v1"

/* ---- prayers ---------------------------------------------------------- */
typedef enum {
    P_FAJR = 0, P_SUNRISE, P_DHUHR, P_ASR, P_MAGHRIB, P_ISHA,
    P_JUMUAH1, P_JUMUAH2, P_COUNT
} Prayer;

extern const wchar_t *kPrayerName[P_COUNT];   /* "Fajr", "Jumu'ah 1", ... */
extern const wchar_t *kPrayerKey[P_COUNT];    /* ini keys: "fajr", ...    */

#define NO_TIME (-1)

/* Minutes from local midnight, or NO_TIME. */
typedef struct {
    int      y, m, d;          /* gregorian date */
    int      dow;              /* 0 = Sunday */
    int      adhan[P_COUNT];
    int      iqamah[P_COUNT];
    wchar_t  hijri[64];        /* e.g. "6 Rabi Al-Thani 1448" */
} DayTimes;

#define MAX_DAYS 21
typedef struct {
    DayTimes days[MAX_DAYS];
    int      count;
    int64_t  fetchedAt;        /* unix seconds, 0 = never */
} Schedule;

/* ---- config ----------------------------------------------------------- */
#define FRIDAY_JUMUAH1 0
#define FRIDAY_JUMUAH2 1
#define FRIDAY_BOTH    2
#define FRIDAY_DHUHR   3

typedef struct {
    int  lead1On,  lead1Min;      /* first reminder,  minutes before iqamah */
    int  lead2On,  lead2Min;      /* second reminder                        */
    int  atIqamah;                /* also alert exactly at iqamah time      */
    int  sound;
    int  toastSeconds;            /* auto-dismiss; 0 = stay until dismissed */
    int  snoozeMinutes;
    int  prayerOn[P_COUNT];
    int  prayerLead[P_COUNT];     /* -1 = use global lead times             */
    int  fridayMode;
    int  autoStart;
    int  use24h;
    int  refreshHour;             /* daily background refresh, 0-23         */
    int  timeShift;               /* minutes added to every time            */
    int  startMinimized;

    /* [masjid] - which masjid this build follows. Edit config.ini to point
     * the app at your own; see README. */
    wchar_t  masjidName[96];      /* header eyebrow, e.g. "Masjid Al-Noor"  */
    wchar_t  website[256];        /* "Open website" target                  */
    wchar_t  masjidalId[32];      /* masjid_id in the Masjidal feed URL     */
    COLORREF accent;              /* brand colour, RRGGBB hex in the ini    */
} Config;

extern Config   g_cfg;
extern Schedule g_sched;

void     config_defaults(Config *c);
void     config_load(Config *c);
void     config_save(const Config *c);
void     config_apply_autostart(const Config *c);
int      config_changed_on_disk(void);        /* ini edited since load/save */
void     config_open_in_editor(void);
wchar_t *app_data_path(const wchar_t *leaf, wchar_t *buf, size_t cch);

/* ---- network / data --------------------------------------------------- */

/* Blocking HTTPS GET. Returns malloc'd UTF-8 body (caller frees) or NULL. */
char *net_get(const wchar_t *host, const wchar_t *path, int *outLen);

int  times_parse_json(const char *json, Schedule *out);   /* 1 = ok */
int  times_load_cache(Schedule *out);
void times_save_cache(const char *json, int len);
void times_fetch_async(HWND notify);                      /* posts WM_APP_FETCHED */
const DayTimes *times_for_date(const Schedule *s, int y, int m, int d);
const DayTimes *times_today(const Schedule *s);

/* ---- scheduling ------------------------------------------------------- */
typedef struct {
    int64_t when;      /* unix seconds, local */
    int     prayer;
    int     lead;      /* minutes before iqamah; 0 = at iqamah time */
    int     iqamahMin;
    int     y, m, d;
} Event;

int  sched_build(Event *out, int cap);          /* today + tomorrow, sorted */
int  sched_next(Event *out);                    /* next upcoming, 1 = found */
int  sched_is_active_today(const DayTimes *dt, int prayer);

/* ---- ui --------------------------------------------------------------- */
#define WM_APP_FETCHED  (WM_APP + 1)
#define WM_APP_TRAY     (WM_APP + 2)
#define WM_APP_SHOWMAIN (WM_APP + 3)

void toast_show(const Event *ev);
void toast_hide(void);
void toast_register(HINSTANCE hi);

void mainwnd_register(HINSTANCE hi);
HWND mainwnd_create(HINSTANCE hi);
void mainwnd_show(void);
void mainwnd_goto_settings(void);
void mainwnd_refresh(void);

extern HWND g_mainWnd;
extern HINSTANCE g_inst;
extern int  g_fetchState;   /* 0 idle, 1 fetching, 2 error */

/* ---- misc helpers ----------------------------------------------------- */
void    fmt_time(int minutes, wchar_t *out, size_t cch);      /* honours use24h */
void    fmt_countdown(int64_t secs, wchar_t *out, size_t cch);
int64_t local_now(void);
void    local_ymd(int *y, int *m, int *d, int *dow);
int64_t ymd_hm_to_unix(int y, int m, int d, int minutes);
void    play_alert(void);
void    open_url(const wchar_t *url);

#endif /* APP_H */
