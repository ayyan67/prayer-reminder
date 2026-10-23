#include "gfx.h"
#include <stdio.h>
#include <string.h>

HWND g_mainWnd;
int  g_fetchState;

/* ---- page + widget model --------------------------------------------- */

enum { PAGE_TODAY = 0, PAGE_SETTINGS = 1 };

enum {                      /* widget kinds */
    W_TOGGLE = 1, W_STEPPER, W_BUTTON, W_SEG, W_PRAYER, W_INFO
};
enum {                      /* stepper value formatting */
    FMT_MIN = 0, FMT_SEC, FMT_LEAD, FMT_HOUR, FMT_SHIFT
};
enum {                      /* button ids */
    BTN_REFRESH = 1, BTN_HIDE, BTN_SITE, BTN_TAB_TODAY, BTN_TAB_SETTINGS,
    BTN_PREVIEW, BTN_EDIT_CFG, BTN_DATA_DIR
};

typedef struct {
    int      type;
    int      id;
    RECT     row;           /* full row, used for hover + label clicks    */
    RECT     ctl;           /* toggle / stepper / button box              */
    RECT     minus, plus;   /* stepper hit zones                          */
    int     *val;
    int      lo, hi, step, fmt;
    int      prayer;        /* W_PRAYER: which prayer this row is         */
    const wchar_t *label;
    const wchar_t *sub;
} W;

#define MAXW 96
static W    s_w[MAXW];
static int  s_nw;
static int  s_page;
static int  s_scroll, s_contentH, s_viewH;
static int  s_hot = -1, s_hotPart, s_pressedIdx = -1, s_pressedPart;
static int  s_shadowLead1, s_shadowLead2, s_shadowPLead[P_COUNT];
static RECT s_contentRect;

/* Rows the Today page drew, so the next-prayer highlight can be found. */
static int  s_nextPrayer = -1;

static W *wadd(int type, int id)
{
    if (s_nw >= MAXW) return &s_w[MAXW - 1];
    W *w = &s_w[s_nw++];
    ZeroMemory(w, sizeof *w);
    w->type = type;
    w->id   = id;
    w->prayer = -1;
    return w;
}

/* ---- value formatting ------------------------------------------------- */

static void fmt_value(const W *w, wchar_t *out, size_t cch)
{
    int v = *w->val;
    switch (w->fmt) {
    case FMT_MIN:
        if (v <= 0) wcscpy_s(out, cch, L"Off");
        else _snwprintf_s(out, cch, _TRUNCATE, L"%d min", v);
        break;
    case FMT_SEC:
        if (v <= 0) wcscpy_s(out, cch, L"Never");
        else _snwprintf_s(out, cch, _TRUNCATE, L"%d sec", v);
        break;
    case FMT_LEAD:
        if (v == -2)      wcscpy_s(out, cch, L"Off");
        else if (v == -1) wcscpy_s(out, cch, L"Default");
        else _snwprintf_s(out, cch, _TRUNCATE, L"%d min", v);
        break;
    case FMT_HOUR: {
        wchar_t t[32];
        fmt_time(v * 60, t, 32);
        wcscpy_s(out, cch, t);
        break;
    }
    case FMT_SHIFT:
        _snwprintf_s(out, cch, _TRUNCATE, L"%+d min", v);
        break;
    default:
        _snwprintf_s(out, cch, _TRUNCATE, L"%d", v);
    }
}

/* ---- config <-> widget shadows ---------------------------------------- */

static void shadows_pull(void)
{
    s_shadowLead1 = g_cfg.lead1On ? g_cfg.lead1Min : 0;
    s_shadowLead2 = g_cfg.lead2On ? g_cfg.lead2Min : 0;
    for (int p = 0; p < P_COUNT; p++)
        s_shadowPLead[p] = g_cfg.prayerOn[p] ? g_cfg.prayerLead[p] : -2;
}

static void shadows_push(void)
{
    g_cfg.lead1On  = s_shadowLead1 > 0;
    if (s_shadowLead1 > 0) g_cfg.lead1Min = s_shadowLead1;
    g_cfg.lead2On  = s_shadowLead2 > 0;
    if (s_shadowLead2 > 0) g_cfg.lead2Min = s_shadowLead2;

    for (int p = 0; p < P_COUNT; p++) {
        if (s_shadowPLead[p] == -2) {
            g_cfg.prayerOn[p] = 0;
        } else {
            g_cfg.prayerOn[p]   = 1;
            g_cfg.prayerLead[p] = s_shadowPLead[p];
        }
    }
}

/* ---- layout ----------------------------------------------------------- */

static int  PAD(void)      { return SC(20); }
static int  ROW_H(void)    { return SC(46); }
static int  CTL_W(void)    { return SC(104); }
static int  TOG_W(void)    { return SC(42); }
static int  TOG_H(void)    { return SC(24); }

static void layout_stepper(W *w, int right, int cy)
{
    int h = SC(30), width = CTL_W();
    w->ctl.right  = right;
    w->ctl.left   = right - width;
    w->ctl.top    = cy - h / 2;
    w->ctl.bottom = w->ctl.top + h;

    int btn = SC(30);
    w->minus = w->ctl; w->minus.right = w->minus.left + btn;
    w->plus  = w->ctl; w->plus.left   = w->plus.right - btn;
}

static void layout_toggle(W *w, int right, int cy)
{
    w->ctl.right  = right;
    w->ctl.left   = right - TOG_W();
    w->ctl.top    = cy - TOG_H() / 2;
    w->ctl.bottom = w->ctl.top + TOG_H();
}

/* Header + next-up card occupy the top; both pages share them. */
static int layout_header(int width)
{
    int y = PAD();
    y += SC(18);                       /* eyebrow */
    y += SC(30);                       /* date line */
    y += SC(20);                       /* hijri line */
    y += SC(14);
    y += SC(104);                      /* next-up card */
    y += SC(16);
    y += SC(36);                       /* tab strip */
    (void)width;
    return y + SC(14);
}

static void layout_tabs(int width, int top)
{
    W *a = wadd(W_SEG, BTN_TAB_TODAY);
    W *b = wadd(W_SEG, BTN_TAB_SETTINGS);
    int h = SC(36), half = (width - PAD() * 2) / 2;
    a->ctl.left = PAD();          a->ctl.right = PAD() + half;
    b->ctl.left = a->ctl.right;   b->ctl.right = width - PAD();
    a->ctl.top = b->ctl.top = top;
    a->ctl.bottom = b->ctl.bottom = top + h;
    a->label = L"Today";
    b->label = L"Settings";
    a->row = a->ctl; b->row = b->ctl;
}

static void layout_today(int width, int top, int bottom)
{
    int y = bottom - PAD() - SC(36);
    W *r = wadd(W_BUTTON, BTN_REFRESH);
    r->ctl.left = PAD(); r->ctl.right = PAD() + SC(120);
    r->ctl.top = y; r->ctl.bottom = y + SC(36);
    r->label = L"Refresh now";
    r->row = r->ctl;

    W *s = wadd(W_BUTTON, BTN_SITE);
    s->ctl.left = r->ctl.right + SC(10); s->ctl.right = s->ctl.left + SC(132);
    s->ctl.top = y; s->ctl.bottom = y + SC(36);
    s->label = L"Open website";
    s->row = s->ctl;

    W *h = wadd(W_BUTTON, BTN_HIDE);
    h->ctl.right = width - PAD(); h->ctl.left = h->ctl.right - SC(112);
    h->ctl.top = y; h->ctl.bottom = y + SC(36);
    h->label = L"Hide to tray";
    h->row = h->ctl;

    (void)top;
}

static void section_title(int *y, int height) { *y += height; }

static void layout_settings(int width, int top, int bottom)
{
    shadows_pull();

    int left = PAD(), right = width - PAD();
    int y = top - s_scroll;
    int rowH = ROW_H();

    s_viewH = bottom - top - PAD();

    /* --- Reminders --- */
    section_title(&y, SC(30));

    struct { const wchar_t *lab, *sub; int *val; int lo, hi, step, fmt; } st1[] = {
        { L"First reminder",  L"minutes before iqamah", &s_shadowLead1,      0, 120, 5, FMT_MIN },
        { L"Second reminder", L"a last nudge",          &s_shadowLead2,      0, 120, 5, FMT_MIN },
        { L"Auto-dismiss",    L"how long the popup stays", &g_cfg.toastSeconds, 0, 300, 15, FMT_SEC },
        { L"Snooze length",   L"when you hit snooze",   &g_cfg.snoozeMinutes, 1, 30, 1, FMT_MIN },
    };
    for (int i = 0; i < 4; i++) {
        W *w = wadd(W_STEPPER, 0);
        w->row.left = left; w->row.right = right;
        w->row.top = y;     w->row.bottom = y + rowH;
        w->label = st1[i].lab; w->sub = st1[i].sub;
        w->val = st1[i].val; w->lo = st1[i].lo; w->hi = st1[i].hi;
        w->step = st1[i].step; w->fmt = st1[i].fmt;
        layout_stepper(w, right, y + rowH / 2);
        y += rowH;
    }

    struct { const wchar_t *lab, *sub; int *val; } tg1[] = {
        { L"Alert at iqamah time", L"one more when it starts", &g_cfg.atIqamah },
        { L"Play a sound",         L"uses your notification sound", &g_cfg.sound },
    };
    for (int i = 0; i < 2; i++) {
        W *w = wadd(W_TOGGLE, 0);
        w->row.left = left; w->row.right = right;
        w->row.top = y;     w->row.bottom = y + rowH;
        w->label = tg1[i].lab; w->sub = tg1[i].sub;
        w->val = tg1[i].val;
        layout_toggle(w, right, y + rowH / 2);
        y += rowH;
    }

    /* --- Prayers --- */
    y += SC(10);
    section_title(&y, SC(30));

    for (int p = 0; p < P_COUNT; p++) {
        W *w = wadd(W_PRAYER, 0);
        w->prayer = p;
        w->row.left = left; w->row.right = right;
        w->row.top = y;     w->row.bottom = y + rowH;
        w->label = kPrayerName[p];
        w->val = &s_shadowPLead[p];
        w->lo = -2; w->hi = 120; w->step = 5; w->fmt = FMT_LEAD;
        layout_stepper(w, right, y + rowH / 2);
        y += rowH;
    }

    /* --- Friday --- */
    y += SC(10);
    section_title(&y, SC(30));
    {
        W *w = wadd(W_SEG, 100);
        w->row.left = left; w->row.right = right;
        w->row.top = y;     w->row.bottom = y + SC(36);
        w->ctl = w->row;
        w->val = &g_cfg.fridayMode;
        y += SC(36) + SC(6);
    }

    /* --- General --- */
    y += SC(10);
    section_title(&y, SC(30));

    struct { const wchar_t *lab, *sub; int *val; } tg2[] = {
        { L"Start with Windows",  L"launch quietly at sign-in", &g_cfg.autoStart },
        { L"Start minimised",     L"go straight to the tray",   &g_cfg.startMinimized },
        { L"24-hour clock",       L"show 17:45 instead of 5:45 PM", &g_cfg.use24h },
    };
    for (int i = 0; i < 3; i++) {
        W *w = wadd(W_TOGGLE, 0);
        w->row.left = left; w->row.right = right;
        w->row.top = y;     w->row.bottom = y + rowH;
        w->label = tg2[i].lab; w->sub = tg2[i].sub;
        w->val = tg2[i].val;
        layout_toggle(w, right, y + rowH / 2);
        y += rowH;
    }
    {
        W *w = wadd(W_STEPPER, 0);
        w->row.left = left; w->row.right = right;
        w->row.top = y;     w->row.bottom = y + rowH;
        w->label = L"Daily refresh";
        w->sub   = L"when to pull fresh times";
        w->val = &g_cfg.refreshHour; w->lo = 0; w->hi = 23; w->step = 1;
        w->fmt = FMT_HOUR;
        layout_stepper(w, right, y + rowH / 2);
        y += rowH;

        W *o = wadd(W_STEPPER, 0);
        o->row.left = left; o->row.right = right;
        o->row.top = y;     o->row.bottom = y + rowH;
        o->label = L"Time offset";
        o->sub   = L"nudge every time if your clock differs";
        o->val = &g_cfg.timeShift; o->lo = -60; o->hi = 60; o->step = 1;
        o->fmt = FMT_SHIFT;
        layout_stepper(o, right, y + rowH / 2);
        y += rowH;
    }

    /* --- Masjid --- */
    y += SC(10);
    section_title(&y, SC(30));
    {
        static wchar_t sub[160];
        _snwprintf_s(sub, 160, _TRUNCATE, L"Masjidal id %s \u00b7 %s",
                     g_cfg.masjidalId, g_cfg.website);
        W *w = wadd(W_INFO, 0);
        w->row.left = left; w->row.right = right;
        w->row.top = y;     w->row.bottom = y + rowH;
        w->label = g_cfg.masjidName;
        w->sub   = sub;
        y += rowH;

        W *e = wadd(W_BUTTON, BTN_EDIT_CFG);
        e->ctl.left = left; e->ctl.right = left + SC(132);
        e->ctl.top = y + SC(4); e->ctl.bottom = e->ctl.top + SC(36);
        e->label = L"Edit config.ini";
        e->row = e->ctl;

        W *f = wadd(W_BUTTON, BTN_DATA_DIR);
        f->ctl.left = e->ctl.right + SC(10); f->ctl.right = f->ctl.left + SC(132);
        f->ctl.top = e->ctl.top; f->ctl.bottom = e->ctl.bottom;
        f->label = L"Open data folder";
        f->row = f->ctl;
        y += SC(4) + SC(36);
    }

    /* --- footer buttons --- */
    y += SC(14);
    {
        W *r = wadd(W_BUTTON, BTN_REFRESH);
        r->ctl.left = left; r->ctl.right = left + SC(120);
        r->ctl.top = y; r->ctl.bottom = y + SC(36);
        r->label = L"Refresh now";
        r->row = r->ctl;

        W *s = wadd(W_BUTTON, BTN_PREVIEW);
        s->ctl.left = r->ctl.right + SC(10); s->ctl.right = s->ctl.left + SC(140);
        s->ctl.top = y; s->ctl.bottom = y + SC(36);
        s->label = L"Preview reminder";
        s->row = s->ctl;
        y += SC(36);
    }
    y += SC(24);

    s_contentH = y + s_scroll - top;
}

static void build_ui(HWND hwnd)
{
    RECT c;
    GetClientRect(hwnd, &c);
    s_nw = 0;

    int top = layout_header(c.right);
    layout_tabs(c.right, top - SC(14) - SC(36));

    s_contentRect.left = 0; s_contentRect.right = c.right;
    s_contentRect.top = top; s_contentRect.bottom = c.bottom;

    if (s_page == PAGE_TODAY) {
        s_contentH = 0;
        layout_today(c.right, top, c.bottom);
    } else {
        layout_settings(c.right, top, c.bottom);
        int maxScroll = s_contentH - (c.bottom - top);
        if (maxScroll < 0) maxScroll = 0;
        if (s_scroll > maxScroll) {
            s_scroll = maxScroll;
            s_nw = 0;
            layout_header(c.right);
            layout_tabs(c.right, top - SC(14) - SC(36));
            layout_settings(c.right, top, c.bottom);
        }
    }
}

/* ---- painting --------------------------------------------------------- */

static void draw_section(HDC dc, const wchar_t *title, int x, int y, int w)
{
    RECT r = { x, y, x + w, y + SC(24) };
    gfx_text(dc, title, r, gfx_font(8, FW_BLD), C_FAINT,
             DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
}

static void draw_stepper(HDC dc, const W *w, COLORREF bg)
{
    int hot = (s_hot == (int)(w - s_w));
    gfx_round_rect(dc, w->ctl, SC(8), hot ? C_CARD_HI : C_CARD, bg);
    gfx_round_outline(dc, w->ctl, SC(8), C_LINE, hot ? C_CARD_HI : C_CARD);

    int atLo = (*w->val <= w->lo), atHi = (*w->val >= w->hi);
    int pm   = (s_pressedIdx == (int)(w - s_w));

    COLORREF cm = atLo ? C_FAINT : ((hot && s_hotPart == 1) ? C_ACCENT : C_DIM);
    COLORREF cp = atHi ? C_FAINT : ((hot && s_hotPart == 2) ? C_ACCENT : C_DIM);
    if (pm && s_pressedPart == 1) cm = C_ACCENT;
    if (pm && s_pressedPart == 2) cp = C_ACCENT;

    RECT m = w->minus; m.top += SC(6);
    RECT p = w->plus;  p.top += SC(6);
    gfx_text(dc, L"\x2212", m, gfx_font(11, FW_MED), cm,
             DT_CENTER | DT_TOP | DT_SINGLELINE);
    gfx_text(dc, L"+", p, gfx_font(11, FW_MED), cp,
             DT_CENTER | DT_TOP | DT_SINGLELINE);

    wchar_t v[32];
    fmt_value(w, v, 32);
    int off = (*w->val <= 0 && (w->fmt == FMT_MIN || w->fmt == FMT_SEC)) ||
              (w->fmt == FMT_LEAD && *w->val == -2);
    RECT t = w->ctl; t.top += SC(6);
    gfx_text(dc, v, t, gfx_font(9, FW_MED), off ? C_FAINT : C_TEXT,
             DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
}

static void draw_row_label(HDC dc, const W *w, COLORREF col)
{
    RECT t = w->row;
    t.left += SC(2);
    if (w->sub) {
        t.top += SC(7);
        gfx_text(dc, w->label, t, gfx_font(10, FW_MED), col,
                 DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        RECT s = w->row; s.left += SC(2); s.top += SC(25);
        gfx_text(dc, w->sub, s, gfx_font(8, FW_REG), C_FAINT,
                 DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    } else {
        t.top += (w->row.bottom - w->row.top - SC(18)) / 2;
        gfx_text(dc, w->label, t, gfx_font(10, FW_MED), col,
                 DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    }
}

static const wchar_t *kFridayLabels[4] = { L"Jumu'ah 1", L"Jumu'ah 2", L"Both", L"Dhuhr" };

static void draw_segment(HDC dc, const W *w, COLORREF bg)
{
    int n = (w->id == 100) ? 4 : 0;
    if (n == 0) return;
    gfx_round_rect(dc, w->ctl, SC(9), C_CARD, bg);
    int width = (w->ctl.right - w->ctl.left) / n;
    for (int i = 0; i < n; i++) {
        RECT cell = w->ctl;
        cell.left  = w->ctl.left + i * width;
        cell.right = (i == n - 1) ? w->ctl.right : cell.left + width;
        int sel = (*w->val == i);
        if (sel) {
            RECT in = cell;
            InflateRect(&in, -SC(3), -SC(3));
            gfx_round_rect(dc, in, SC(7), C_ACCENT, C_CARD);
        }
        RECT t = cell;
        t.top += (cell.bottom - cell.top - SC(17)) / 2;
        gfx_text(dc, kFridayLabels[i], t, gfx_font(9, sel ? FW_MED : FW_REG),
                 sel ? gfx_on_accent() : C_DIM,
                 DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    }
}

static void draw_tabs(HDC dc)
{
    for (int i = 0; i < s_nw; i++) {
        W *w = &s_w[i];
        if (w->type != W_SEG || (w->id != BTN_TAB_TODAY && w->id != BTN_TAB_SETTINGS))
            continue;
        int sel = (w->id == BTN_TAB_TODAY) ? (s_page == PAGE_TODAY)
                                           : (s_page == PAGE_SETTINGS);
        if (sel) gfx_round_rect(dc, w->ctl, SC(9), C_CARD_HI, C_BG);
        else if (s_hot == i) gfx_round_rect(dc, w->ctl, SC(9), C_CARD, C_BG);

        RECT t = w->ctl;
        t.top += (w->ctl.bottom - w->ctl.top - SC(18)) / 2;
        gfx_text(dc, w->label, t, gfx_font(10, sel ? FW_MED : FW_REG),
                 sel ? C_TEXT : C_DIM,
                 DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    }
    /* hairline under the strip */
    for (int i = 0; i < s_nw; i++) {
        W *w = &s_w[i];
        if (w->type == W_SEG && w->id == BTN_TAB_SETTINGS) {
            RECT l = { PAD(), w->ctl.bottom + SC(13),
                       w->ctl.right, w->ctl.bottom + SC(13) + 1 };
            gfx_fill(dc, l, C_LINE);
        }
    }
}

static void ago_text(int64_t when, wchar_t *out, size_t cch)
{
    if (when == 0) { wcscpy_s(out, cch, L"never"); return; }
    int64_t s = local_now() - when;
    if (s < 90)         wcscpy_s(out, cch, L"just now");
    else if (s < 5400)  _snwprintf_s(out, cch, _TRUNCATE, L"%lld min ago", s / 60);
    else if (s < 172800)_snwprintf_s(out, cch, _TRUNCATE, L"%lld h ago", s / 3600);
    else                _snwprintf_s(out, cch, _TRUNCATE, L"%lld days ago", s / 86400);
}

static void draw_header(HDC dc, int width)
{
    static const wchar_t *kDow[7] = { L"Sunday", L"Monday", L"Tuesday",
        L"Wednesday", L"Thursday", L"Friday", L"Saturday" };
    static const wchar_t *kMon[12] = { L"January", L"February", L"March",
        L"April", L"May", L"June", L"July", L"August", L"September",
        L"October", L"November", L"December" };

    int y = PAD();
    RECT r = { PAD(), y, width - PAD(), y + SC(18) };
    wchar_t eyebrow[96];
    wcscpy_s(eyebrow, 96, g_cfg.masjidName);
    CharUpperW(eyebrow);
    gfx_text(dc, eyebrow, r, gfx_font(8, FW_BLD),
             C_ACCENT, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    y += SC(18);

    int yy, mm, dd, dow;
    local_ymd(&yy, &mm, &dd, &dow);
    wchar_t line[128];
    _snwprintf_s(line, 128, _TRUNCATE, L"%s, %s %d",
                 kDow[dow % 7], kMon[(mm - 1) % 12], dd);
    RECT d = { PAD(), y, width - PAD(), y + SC(30) };
    gfx_text(dc, line, d, gfx_font(17, FW_MED), C_TEXT,
             DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    y += SC(30);

    const DayTimes *dt = times_today(&g_sched);
    wchar_t sub[160];
    if (dt && dt->hijri[0]) _snwprintf_s(sub, 160, _TRUNCATE, L"%s", dt->hijri);
    else                    wcscpy_s(sub, 160, L"");
    RECT h = { PAD(), y, width - PAD(), y + SC(20) };
    gfx_text(dc, sub, h, gfx_font(9, FW_REG), C_DIM,
             DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
}

static void draw_next_card(HDC dc, int width)
{
    int top = PAD() + SC(18) + SC(30) + SC(20) + SC(14);
    RECT card = { PAD(), top, width - PAD(), top + SC(104) };
    gfx_round_rect(dc, card, SC(12), C_CARD, C_BG);

    /* accent rail */
    RECT rail = { card.left, card.top + SC(16), card.left + SC(3),
                  card.bottom - SC(16) };
    gfx_round_rect(dc, rail, SC(2), C_ACCENT, C_CARD);

    int x = card.left + SC(20);
    Event ev;
    int have = (g_sched.count > 0) && sched_next(&ev);

    if (!have) {
        RECT a = { x, card.top + SC(30), card.right - SC(20), card.top + SC(56) };
        gfx_text(dc, g_fetchState == 1 ? L"Fetching times\x2026"
                                       : L"No times yet",
                 a, gfx_font(15, FW_MED), C_TEXT,
                 DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        RECT b = { x, card.top + SC(58), card.right - SC(20), card.top + SC(80) };
        gfx_text(dc, g_fetchState == 2 ? L"Could not reach the site \x2014 retrying"
                                       : L"Hang on a moment",
                 b, gfx_font(9, FW_REG), C_DIM,
                 DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    s_nextPrayer = ev.prayer;

    wchar_t eyebrow[64];
    _snwprintf_s(eyebrow, 64, _TRUNCATE, L"NEXT UP  \x00B7  %s", kPrayerName[ev.prayer]);
    RECT e = { x, card.top + SC(16), card.right - SC(20), card.top + SC(32) };
    gfx_text(dc, eyebrow, e, gfx_font(8, FW_BLD), C_ACCENT,
             DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    wchar_t t[32];
    fmt_time(ev.iqamahMin, t, 32);
    RECT tr = { x, card.top + SC(34), card.right - SC(20), card.top + SC(76) };
    gfx_text(dc, t, tr, gfx_font(26, FW_BLD), C_TEXT,
             DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    /* countdown to the prayer itself, not to the reminder */
    int64_t target = ymd_hm_to_unix(ev.y, ev.m, ev.d, ev.iqamahMin);
    wchar_t cd[64], line[160];
    fmt_countdown(target - local_now(), cd, 64);

    if (ev.lead > 0) {
        wchar_t rt[32];
        fmt_time(ev.iqamahMin - ev.lead, rt, 32);
        _snwprintf_s(line, 160, _TRUNCATE, L"in %s  \x00B7  reminder at %s", cd, rt);
    } else {
        _snwprintf_s(line, 160, _TRUNCATE, L"in %s", cd);
    }
    RECT lr = { x, card.top + SC(78), card.right - SC(20), card.top + SC(98) };
    gfx_text(dc, line, lr, gfx_font(9, FW_REG), C_DIM,
             DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
}

static void draw_today(HDC dc, int width, int top, int bottom)
{
    const DayTimes *dt = times_today(&g_sched);
    int y = top;

    RECT hdr = { PAD(), y, width - PAD(), y + SC(18) };
    gfx_text(dc, L"PRAYER", hdr, gfx_font(8, FW_BLD), C_FAINT,
             DT_LEFT | DT_TOP | DT_SINGLELINE);
    RECT hi = hdr; hi.right = width - PAD() - SC(96);
    gfx_text(dc, L"ADHAN", hi, gfx_font(8, FW_BLD), C_FAINT,
             DT_RIGHT | DT_TOP | DT_SINGLELINE);
    RECT hq = hdr; hq.right -= SC(14);
    gfx_text(dc, L"IQAMAH", hq, gfx_font(8, FW_BLD), C_FAINT,
             DT_RIGHT | DT_TOP | DT_SINGLELINE);
    y += SC(24);

    if (!dt) {
        RECT m = { PAD(), y + SC(20), width - PAD(), y + SC(60) };
        gfx_text(dc, L"Times unavailable. Try Refresh.", m,
                 gfx_font(10, FW_REG), C_DIM, DT_LEFT | DT_TOP | DT_SINGLELINE);
        return;
    }

    int rowH = SC(42);
    for (int p = 0; p < P_COUNT; p++) {
        if (!sched_is_active_today(dt, p)) continue;
        int tgt = (p == P_SUNRISE) ? dt->adhan[P_SUNRISE] : dt->iqamah[p];
        if (tgt == NO_TIME && dt->adhan[p] == NO_TIME) continue;

        RECT row = { PAD(), y, width - PAD(), y + rowH };
        int isNext = (p == s_nextPrayer);
        if (isNext) gfx_round_rect(dc, row, SC(10), C_CARD, C_BG);

        int enabled = g_cfg.prayerOn[p];
        COLORREF nameCol = enabled ? C_TEXT : C_FAINT;

        if (isNext) {
            gfx_circle(dc, row.left + SC(14), y + rowH / 2, SC(3), C_ACCENT, C_CARD);
        } else if (!enabled) {
            gfx_circle(dc, row.left + SC(14), y + rowH / 2, SC(3),
                       RGB(0x2C, 0x32, 0x3A), C_BG);
        }

        RECT n = row;
        n.left += SC(28);
        n.top  += (rowH - SC(19)) / 2;
        gfx_text(dc, kPrayerName[p], n, gfx_font(11, isNext ? FW_MED : FW_REG),
                 nameCol, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

        wchar_t a[32], q[32];
        fmt_time(dt->adhan[p], a, 32);
        fmt_time(tgt, q, 32);

        RECT ar = row;
        ar.right = width - PAD() - SC(96);
        ar.top  += (rowH - SC(18)) / 2;
        /* Sunrise has no congregation, so its one time sits in the main
         * column rather than being printed twice. */
        if (dt->adhan[p] != NO_TIME && p != P_SUNRISE)
            gfx_text(dc, a, ar, gfx_font(9, FW_REG), C_DIM,
                     DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

        RECT qr = row;
        qr.right -= SC(14);
        qr.top   += (rowH - SC(19)) / 2;
        if (tgt != NO_TIME)
            gfx_text(dc, q, qr, gfx_font(11, FW_MED),
                     isNext ? C_ACCENT : (enabled ? C_TEXT : C_FAINT),
                     DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        y += rowH;
    }

    /* status line above the footer buttons */
    wchar_t ago[64], status[160];
    ago_text(g_sched.fetchedAt, ago, 64);
    if (g_fetchState == 1)
        wcscpy_s(status, 160, L"Refreshing\x2026");
    else if (g_fetchState == 2)
        _snwprintf_s(status, 160, _TRUNCATE,
                     L"Offline \x2014 showing cached times (updated %s)", ago);
    else
        _snwprintf_s(status, 160, _TRUNCATE,
                     L"Updated %s \x00B7 %d days cached", ago, g_sched.count);

    RECT s = { PAD(), bottom - PAD() - SC(36) - SC(26), width - PAD(),
               bottom - PAD() - SC(36) - SC(6) };
    gfx_text(dc, status, s, gfx_font(8, FW_REG),
             g_fetchState == 2 ? C_WARN : C_FAINT,
             DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
}

static void draw_scrollbar(HDC dc, int width, int top, int bottom)
{
    int viewH = bottom - top;
    if (s_contentH <= viewH) return;

    int trackX = width - SC(7);
    int thumbH = viewH * viewH / s_contentH;
    if (thumbH < SC(30)) thumbH = SC(30);
    int maxScroll = s_contentH - viewH;
    int thumbY = top + (viewH - thumbH) * s_scroll / (maxScroll ? maxScroll : 1);

    RECT th = { trackX, thumbY, trackX + SC(4), thumbY + thumbH };
    gfx_round_rect(dc, th, SC(2), RGB(0x3A, 0x42, 0x4C), C_BG);
}

static void paint(HWND hwnd, HDC target)
{
    RECT c;
    GetClientRect(hwnd, &c);

    HDC     mem = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, c.right, c.bottom);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);

    gfx_fill(mem, c, C_BG);
    draw_header(mem, c.right);
    draw_next_card(mem, c.right);
    draw_tabs(mem);

    int top = s_contentRect.top;
    HRGN clip = CreateRectRgn(0, top, c.right, c.bottom);
    SelectClipRgn(mem, clip);

    if (s_page == PAGE_TODAY) {
        draw_today(mem, c.right, top, c.bottom);
    } else {
        /* section headings sit just above their first row */
        static const wchar_t *titles[5] =
            { L"REMINDERS", L"PRAYERS", L"FRIDAY", L"GENERAL", L"MASJID" };
        /* Each heading sits above the first widget of its group; the groups
         * are laid out in a fixed order, so one pass in order finds them. */
        int ti = 0;
        for (int i = 0; i < s_nw && ti < 5; i++) {
            W *w = &s_w[i];
            if (w->type == W_SEG &&
                (w->id == BTN_TAB_TODAY || w->id == BTN_TAB_SETTINGS)) continue;
            int group = -1;
            if (w->type == W_STEPPER && ti == 0) group = 0;
            else if (w->type == W_PRAYER && ti == 1) group = 1;
            else if (w->type == W_SEG && w->id == 100 && ti == 2) group = 2;
            else if (w->type == W_TOGGLE && ti == 3) group = 3;
            else if (w->type == W_INFO && ti == 4) group = 4;
            if (group == ti) {
                draw_section(mem, titles[ti], PAD(), w->row.top - SC(24),
                             c.right - PAD() * 2);
                ti++;
            }
        }

        for (int i = 0; i < s_nw; i++) {
            W *w = &s_w[i];
            if (w->type == W_SEG &&
                (w->id == BTN_TAB_TODAY || w->id == BTN_TAB_SETTINGS)) continue;
            if (w->row.bottom < top || w->row.top > c.bottom) continue;

            int hot = (s_hot == i);
            if (hot && (w->type == W_TOGGLE || w->type == W_PRAYER ||
                        w->type == W_STEPPER))
                gfx_round_rect(mem, w->row, SC(10), RGB(0x16, 0x1A, 0x20), C_BG);

            switch (w->type) {
            case W_TOGGLE:
                draw_row_label(mem, w, C_TEXT);
                gfx_toggle(mem, w->ctl, *w->val, hot,
                           hot ? RGB(0x16, 0x1A, 0x20) : C_BG);
                break;
            case W_STEPPER:
                draw_row_label(mem, w, C_TEXT);
                draw_stepper(mem, w, hot ? RGB(0x16, 0x1A, 0x20) : C_BG);
                break;
            case W_PRAYER: {
                int on = (*w->val != -2);
                draw_row_label(mem, w, on ? C_TEXT : C_FAINT);
                const DayTimes *dt = times_today(&g_sched);
                if (dt) {
                    int tgt = (w->prayer == P_SUNRISE) ? dt->adhan[P_SUNRISE]
                                                       : dt->iqamah[w->prayer];
                    if (tgt != NO_TIME) {
                        wchar_t t[32];
                        fmt_time(tgt, t, 32);
                        RECT tr = w->row;
                        tr.right = w->ctl.left - SC(14);
                        tr.top  += (w->row.bottom - w->row.top - SC(17)) / 2;
                        gfx_text(mem, t, tr, gfx_font(9, FW_REG), C_FAINT,
                                 DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
                    }
                }
                draw_stepper(mem, w, hot ? RGB(0x16, 0x1A, 0x20) : C_BG);
                break;
            }
            case W_SEG:
                draw_segment(mem, w, C_BG);
                break;
            case W_INFO:
                draw_row_label(mem, w, C_TEXT);
                break;
            case W_BUTTON:
                gfx_button(mem, w->ctl, w->label, hot, s_pressedIdx == i,
                           w->id == BTN_REFRESH, C_BG);
                break;
            }
        }
        draw_scrollbar(mem, c.right, top, c.bottom);
    }

    SelectClipRgn(mem, NULL);
    DeleteObject(clip);

    if (s_page == PAGE_TODAY) {
        for (int i = 0; i < s_nw; i++) {
            W *w = &s_w[i];
            if (w->type == W_BUTTON)
                gfx_button(mem, w->ctl, w->label, s_hot == i, s_pressedIdx == i,
                           w->id == BTN_REFRESH, C_BG);
        }
    }

    BitBlt(target, 0, 0, c.right, c.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

/* ---- interaction ------------------------------------------------------ */

static int hit_test(POINT p, int *part)
{
    *part = 0;
    for (int i = s_nw - 1; i >= 0; i--) {
        W *w = &s_w[i];
        RECT probe = (w->type == W_BUTTON || w->type == W_SEG) ? w->ctl : w->row;
        if (!PtInRect(&probe, p)) continue;
        if (w->type == W_STEPPER || w->type == W_PRAYER) {
            if (PtInRect((RECT *)&w->minus, p)) *part = 1;
            else if (PtInRect((RECT *)&w->plus, p)) *part = 2;
            else if (!PtInRect((RECT *)&w->ctl, p)) *part = 0;
            else *part = 3;                       /* inert middle of stepper */
        }
        return i;
    }
    return -1;
}

static void step_value(W *w, int dir)
{
    int v = *w->val;
    if (w->fmt == FMT_LEAD) {
        /* Off -> Default -> 5 -> 10 ... */
        if (dir > 0) {
            if (v == -2) v = -1;
            else if (v == -1) v = 5;
            else v += w->step;
        } else {
            if (v <= 5) v = (v == 5) ? -1 : -2;
            else v -= w->step;
        }
    } else {
        v += dir * w->step;
    }
    if (v < w->lo) v = w->lo;
    if (v > w->hi) v = w->hi;
    *w->val = v;
}

static void commit(HWND hwnd)
{
    shadows_push();
    config_save(&g_cfg);
    config_apply_autostart(&g_cfg);
    InvalidateRect(hwnd, NULL, FALSE);
}

static void on_click(HWND hwnd, int idx, int part)
{
    if (idx < 0) return;
    W *w = &s_w[idx];

    switch (w->type) {
    case W_SEG:
        if (w->id == BTN_TAB_TODAY)         { s_page = PAGE_TODAY; s_scroll = 0; }
        else if (w->id == BTN_TAB_SETTINGS) { s_page = PAGE_SETTINGS; s_scroll = 0; }
        else if (w->id == 100) {
            POINT p;
            GetCursorPos(&p); ScreenToClient(hwnd, &p);
            int width = (w->ctl.right - w->ctl.left) / 4;
            int i = (p.x - w->ctl.left) / (width ? width : 1);
            if (i < 0) i = 0;
            if (i > 3) i = 3;
            *w->val = i;
        }
        build_ui(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        if (w->id == 100) commit(hwnd);
        return;

    case W_TOGGLE:
        *w->val = !*w->val;
        commit(hwnd);
        return;

    case W_STEPPER:
    case W_PRAYER:
        /* Only the -/+ controls change a prayer. A stray click on the row
         * must never silently switch a prayer's reminders off. */
        if (part == 1) step_value(w, -1);
        else if (part == 2) step_value(w, +1);
        else return;
        commit(hwnd);
        return;

    case W_BUTTON:
        switch (w->id) {
        case BTN_REFRESH: times_fetch_async(hwnd);
                          InvalidateRect(hwnd, NULL, FALSE); break;
        case BTN_SITE:    open_url(g_cfg.website); break;
        case BTN_HIDE:    ShowWindow(hwnd, SW_HIDE); break;
        case BTN_EDIT_CFG: config_open_in_editor(); break;
        case BTN_DATA_DIR: {
            wchar_t dir[MAX_PATH];
            if (app_data_path(NULL, dir, MAX_PATH)) open_url(dir);
            break;
        }
        case BTN_PREVIEW: {
            /* Show what a real reminder will look like, with real numbers. */
            Event ev;
            if (!sched_next(&ev)) {
                ZeroMemory(&ev, sizeof ev);
                ev.prayer = P_ASR;
                ev.iqamahMin = 17 * 60 + 45;
            }
            if (ev.lead <= 0) ev.lead = g_cfg.lead1On ? g_cfg.lead1Min : 10;
            toast_show(&ev);
            break;
        }
        }
        return;
    }
}

/* ---- window proc ------------------------------------------------------ */

/* The ini was edited by hand (Settings > Edit config.ini). Pick up the new
 * values, and if the masjid changed, drop the old schedule and refetch. */
static void reload_config(HWND hwnd)
{
    wchar_t oldId[32];
    wcscpy_s(oldId, 32, g_cfg.masjidalId);
    config_load(&g_cfg);
    config_apply_autostart(&g_cfg);
    if (wcscmp(oldId, g_cfg.masjidalId) != 0) {
        ZeroMemory(&g_sched, sizeof g_sched);
        times_load_cache(&g_sched);
        times_fetch_async(hwnd);
    }
    build_ui(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE && config_changed_on_disk())
            reload_config(hwnd);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        build_ui(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp);
        gfx_reset_fonts();
        RECT *r = (RECT *)lp;
        SetWindowPos(hwnd, NULL, r->left, r->top,
                     r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        build_ui(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int part, idx = hit_test(p, &part);
        if (idx != s_hot || part != s_hotPart) {
            s_hot = idx; s_hotPart = part;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        s_hot = -1; s_hotPart = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        s_pressedIdx = hit_test(p, &s_pressedPart);
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONUP: {
        ReleaseCapture();
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int part, idx = hit_test(p, &part);
        int wasIdx = s_pressedIdx, wasPart = s_pressedPart;
        s_pressedIdx = -1;
        if (idx >= 0 && idx == wasIdx && part == wasPart)
            on_click(hwnd, idx, part);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (s_page != PAGE_SETTINGS) return 0;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        s_scroll -= delta / WHEEL_DELTA * SC(56);
        if (s_scroll < 0) s_scroll = 0;
        build_ui(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { ShowWindow(hwnd, SW_HIDE); return 0; }
        if (wp == VK_F5)     { times_fetch_async(hwnd); return 0; }
        if (s_page == PAGE_SETTINGS && (wp == VK_NEXT || wp == VK_PRIOR)) {
            s_scroll += (wp == VK_NEXT ? 1 : -1) * SC(300);
            if (s_scroll < 0) s_scroll = 0;
            build_ui(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_CLOSE:                       /* keep running in the tray */
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = SC(440);
        mm->ptMinTrackSize.y = SC(520);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- public ----------------------------------------------------------- */

void mainwnd_register(HINSTANCE hi)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.cbSize        = sizeof wc;
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = main_proc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = APP_CLASS;
    wc.hIcon         = gfx_make_icon(32);
    wc.hIconSm       = gfx_make_icon(16);
    RegisterClassExW(&wc);
}

HWND mainwnd_create(HINSTANCE hi)
{
    HWND h = CreateWindowExW(0, APP_CLASS, APP_NAME,
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 520, 720,
                             NULL, NULL, hi, NULL);
    if (!h) return NULL;

    g_dpi = (int)GetDpiForWindow(h);
    gfx_reset_fonts();

    /* dark title bar + rounded corners where the OS supports them */
    BOOL dark = TRUE;
    DwmSetWindowAttribute(h, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    DWORD corner = 2 /*DWMWCP_ROUND*/;
    DwmSetWindowAttribute(h, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/,
                          &corner, sizeof corner);
    COLORREF cap = C_BG;
    DwmSetWindowAttribute(h, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof cap);

    /* Size from the client area so the frame does not eat into the layout. */
    RECT want = { 0, 0, SC(500), SC(700) };
    AdjustWindowRectExForDpi(&want, WS_OVERLAPPEDWINDOW, FALSE, 0, (UINT)g_dpi);
    SetWindowPos(h, NULL, 0, 0, want.right - want.left, want.bottom - want.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    build_ui(h);
    return h;
}

void mainwnd_show(void)
{
    if (!g_mainWnd) return;
    ShowWindow(g_mainWnd, SW_SHOW);
    SetForegroundWindow(g_mainWnd);
    build_ui(g_mainWnd);
    InvalidateRect(g_mainWnd, NULL, FALSE);
}

void mainwnd_goto_settings(void)
{
    s_page = PAGE_SETTINGS;
    s_scroll = 0;
    mainwnd_show();
}

void mainwnd_refresh(void)
{
    if (!g_mainWnd) return;
    build_ui(g_mainWnd);
    InvalidateRect(g_mainWnd, NULL, FALSE);
}
