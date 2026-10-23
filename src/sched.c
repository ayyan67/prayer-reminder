#include "app.h"
#include <string.h>
#include <time.h>

/* Which congregation actually happens on a given day. Friday swaps Dhuhr
 * for the Jumu'ah the user attends; the rest of the week has no Jumu'ah. */
int sched_is_active_today(const DayTimes *dt, int prayer)
{
    if (!dt) return 0;
    int friday = (dt->dow == 5);

    if (prayer == P_JUMUAH1)
        return friday && (g_cfg.fridayMode == FRIDAY_JUMUAH1 ||
                          g_cfg.fridayMode == FRIDAY_BOTH);
    if (prayer == P_JUMUAH2)
        return friday && (g_cfg.fridayMode == FRIDAY_JUMUAH2 ||
                          g_cfg.fridayMode == FRIDAY_BOTH);
    if (prayer == P_DHUHR && friday)
        return g_cfg.fridayMode == FRIDAY_DHUHR;
    return 1;
}

/* The moment that matters for a given row: iqamah, except Sunrise, which
 * has no congregation and is really "Fajr ends". */
static int target_minute(const DayTimes *dt, int prayer)
{
    int t = (prayer == P_SUNRISE) ? dt->adhan[P_SUNRISE] : dt->iqamah[prayer];
    if (t == NO_TIME) return NO_TIME;
    t += g_cfg.timeShift;
    if (t < 0 || t >= 24 * 60) return NO_TIME;
    return t;
}

static void add_event(Event *out, int cap, int *n, const DayTimes *dt,
                      int prayer, int tgt, int lead)
{
    if (*n >= cap) return;
    int when = tgt - lead;
    if (when < 0) return;

    int64_t ts = ymd_hm_to_unix(dt->y, dt->m, dt->d, when);
    for (int i = 0; i < *n; i++)            /* two leads can collide */
        if (out[i].when == ts && out[i].prayer == prayer) return;

    Event *e = &out[(*n)++];
    e->when      = ts;
    e->prayer    = prayer;
    e->lead      = lead;
    e->iqamahMin = tgt;
    e->y = dt->y; e->m = dt->m; e->d = dt->d;
}

static void events_for_day(const DayTimes *dt, Event *out, int cap, int *n)
{
    for (int p = 0; p < P_COUNT; p++) {
        if (!g_cfg.prayerOn[p])            continue;
        if (!sched_is_active_today(dt, p))  continue;

        int tgt = target_minute(dt, p);
        if (tgt == NO_TIME) continue;

        if (g_cfg.prayerLead[p] >= 0) {
            /* A per-prayer lead replaces the two global reminders. */
            add_event(out, cap, n, dt, p, tgt, g_cfg.prayerLead[p]);
        } else {
            if (g_cfg.lead1On) add_event(out, cap, n, dt, p, tgt, g_cfg.lead1Min);
            if (g_cfg.lead2On) add_event(out, cap, n, dt, p, tgt, g_cfg.lead2Min);
        }
        if (g_cfg.atIqamah) add_event(out, cap, n, dt, p, tgt, 0);
    }
}

static void sort_events(Event *e, int n)
{
    for (int i = 1; i < n; i++) {
        Event k = e[i];
        int j = i - 1;
        while (j >= 0 && e[j].when > k.when) { e[j + 1] = e[j]; j--; }
        e[j + 1] = k;
    }
}

int sched_build(Event *out, int cap)
{
    int n = 0, y, m, d, dow;
    local_ymd(&y, &m, &d, &dow);

    const DayTimes *today = times_for_date(&g_sched, y, m, d);
    if (today) events_for_day(today, out, cap, &n);

    /* Tomorrow too, so the late-night Fajr reminder is already scheduled.
     * mktime normalises the day overflow across month and year ends. */
    struct tm lt;
    memset(&lt, 0, sizeof lt);
    lt.tm_year = y - 1900; lt.tm_mon = m - 1; lt.tm_mday = d + 1;
    lt.tm_hour = 12; lt.tm_isdst = -1;
    mktime(&lt);

    const DayTimes *tomorrow = times_for_date(&g_sched, lt.tm_year + 1900,
                                              lt.tm_mon + 1, lt.tm_mday);
    if (tomorrow) events_for_day(tomorrow, out, cap, &n);

    sort_events(out, n);
    return n;
}

int sched_next(Event *out)
{
    Event ev[128];
    int n = sched_build(ev, 128);
    int64_t now = local_now();
    for (int i = 0; i < n; i++)
        if (ev[i].when > now) { *out = ev[i]; return 1; }
    return 0;
}
