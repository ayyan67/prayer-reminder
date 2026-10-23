#include "gfx.h"
#include <math.h>
#include <stdio.h>

/* The popup is a per-pixel-alpha layered window: we render the card into a
 * 32bpp DIB, compute coverage for the rounded outline ourselves, and hand
 * the whole thing to UpdateLayeredWindow. That gives clean antialiased
 * corners over any wallpaper, which SetWindowRgn cannot. */

#define T_ANIM   1
#define T_AUTO   2
#define T_SNOOZE 3

#define TW 372
#define TH 168

static HWND  s_toast;
static Event s_ev;
static int   s_haveEv;
static int   s_anim;          /* 0..100 */
static int   s_closing;
static RECT  s_btnSnooze, s_btnDismiss;
static int   s_hot = -1, s_pressed = -1;

static void toast_layout(void)
{
    int w = SC(TW), h = SC(TH);
    int bw = SC(96), bh = SC(32);
    int y = h - SC(18) - bh;

    s_btnDismiss.right  = w - SC(18);
    s_btnDismiss.left   = s_btnDismiss.right - bw;
    s_btnDismiss.top    = y;
    s_btnDismiss.bottom = y + bh;

    s_btnSnooze.right  = s_btnDismiss.left - SC(8);
    s_btnSnooze.left   = s_btnSnooze.right - bw;
    s_btnSnooze.top    = y;
    s_btnSnooze.bottom = y + bh;
}

static const wchar_t *target_label(int prayer)
{
    if (prayer == P_SUNRISE)                               return L"Sunrise at %s";
    if (prayer == P_JUMUAH1 || prayer == P_JUMUAH2)        return L"Khutbah at %s";
    return L"Iqamah at %s";
}

/* Coverage of a rounded rectangle at a pixel centre, 0..1. */
static double rr_cov(double x, double y, double w, double h, double r)
{
    double dx = 0, dy = 0;
    if (x < r)         dx = r - x;
    else if (x > w - r) dx = x - (w - r);
    if (y < r)         dy = r - y;
    else if (y > h - r) dy = y - (h - r);
    double d = sqrt(dx * dx + dy * dy);
    double c = r + 0.5 - d;
    if (c < 0) c = 0;
    if (c > 1) c = 1;
    return c;
}

static void toast_render(void)
{
    if (!s_toast) return;
    int w = SC(TW), h = SC(TH);

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC   screen = GetDC(NULL);
    HDC   dc     = CreateCompatibleDC(screen);
    BYTE *bits   = NULL;
    HBITMAP bmp  = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (!bmp) { DeleteDC(dc); ReleaseDC(NULL, screen); return; }
    HBITMAP old  = (HBITMAP)SelectObject(dc, bmp);

    RECT all = { 0, 0, w, h };
    gfx_fill(dc, all, C_CARD);

    /* accent rail */
    RECT rail = { 0, 0, SC(4), h };
    gfx_fill(dc, rail, C_ACCENT);

    int x = SC(22);
    wchar_t buf[192], t[32];

    /* eyebrow */
    if (s_ev.lead <= 0)
        wcscpy_s(buf, 192, L"STARTING NOW");
    else if (s_ev.lead == 1)
        wcscpy_s(buf, 192, L"IN 1 MINUTE");
    else
        _snwprintf_s(buf, 192, _TRUNCATE, L"IN %d MINUTES", s_ev.lead);

    RECT e = { x, SC(20), w - SC(18), SC(36) };
    gfx_text(dc, buf, e, gfx_font(8, FW_BLD), C_ACCENT,
             DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    /* prayer name */
    RECT n = { x, SC(36), w - SC(18), SC(76) };
    gfx_text(dc, kPrayerName[s_ev.prayer], n, gfx_font(22, FW_BLD), C_TEXT,
             DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    /* iqamah line */
    fmt_time(s_ev.iqamahMin, t, 32);
    _snwprintf_s(buf, 192, _TRUNCATE, target_label(s_ev.prayer), t);
    RECT sline = { x, SC(76), w - SC(18), SC(98) };
    gfx_text(dc, buf, sline, gfx_font(10, FW_REG), C_DIM,
             DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    /* buttons */
    _snwprintf_s(buf, 192, _TRUNCATE, L"Snooze %dm", g_cfg.snoozeMinutes);
    gfx_button(dc, s_btnSnooze, buf, s_hot == 0, s_pressed == 0, 0, C_CARD);
    gfx_button(dc, s_btnDismiss, L"Dismiss", s_hot == 1, s_pressed == 1, 1, C_CARD);

    /* alpha mask: rounded card with a hairline edge */
    double radius = SC(14);
    const double bR = 0x36, bG = 0x3E, bB = 0x48;      /* border colour */
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            double cov = rr_cov(px + 0.5, py + 0.5, w, h, radius);
            BYTE *p = bits + (py * (size_t)w + px) * 4;
            if (cov <= 0.0) { p[0] = p[1] = p[2] = p[3] = 0; continue; }

            /* inner shape, inset by ~1.3px, marks where the border sits */
            double inner = rr_cov(px + 0.5 - 1.3, py + 0.5 - 1.3,
                                  w - 2.6, h - 2.6, radius - 1.3);
            double edge = cov - inner;
            if (edge > 0.01) {
                p[0] = (BYTE)(p[0] * (1 - edge) + bB * edge);
                p[1] = (BYTE)(p[1] * (1 - edge) + bG * edge);
                p[2] = (BYTE)(p[2] * (1 - edge) + bR * edge);
            }
            p[0] = (BYTE)(p[0] * cov);      /* premultiply */
            p[1] = (BYTE)(p[1] * cov);
            p[2] = (BYTE)(p[2] * cov);
            p[3] = (BYTE)(cov * 255.0 + 0.5);
        }
    }

    /* slide up as it fades in */
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int rise = SC(18) * (100 - s_anim) / 100;
    POINT pos = { wa.right - w - SC(16), wa.bottom - h - SC(16) + rise };
    SIZE  sz  = { w, h };
    POINT src = { 0, 0 };

    BLENDFUNCTION bf;
    bf.BlendOp             = AC_SRC_OVER;
    bf.BlendFlags          = 0;
    bf.SourceConstantAlpha = (BYTE)(255 * s_anim / 100);
    bf.AlphaFormat         = AC_SRC_ALPHA;

    UpdateLayeredWindow(s_toast, screen, &pos, &sz, dc, &src, 0, &bf, ULW_ALPHA);

    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
}

static void toast_begin_close(void)
{
    if (s_closing) return;
    s_closing = 1;
    SetTimer(s_toast, T_ANIM, 15, NULL);
}

static LRESULT CALLBACK toast_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        if (wp == T_ANIM) {
            s_anim += s_closing ? -10 : 12;
            if (s_anim >= 100) { s_anim = 100; KillTimer(hwnd, T_ANIM); }
            if (s_anim <= 0) {
                s_anim = 0;
                KillTimer(hwnd, T_ANIM);
                ShowWindow(hwnd, SW_HIDE);
                s_closing = 0;
                return 0;
            }
            toast_render();
        } else if (wp == T_AUTO) {
            KillTimer(hwnd, T_AUTO);
            toast_begin_close();
        } else if (wp == T_SNOOZE) {
            KillTimer(hwnd, T_SNOOZE);
            if (s_haveEv) {
                Event e = s_ev;
                e.lead = 0;              /* snoozed alerts say "now" */
                toast_show(&e);
            }
        }
        return 0;

    case WM_MOUSEMOVE: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int h = PtInRect(&s_btnSnooze, p) ? 0 : (PtInRect(&s_btnDismiss, p) ? 1 : -1);
        if (h != s_hot) { s_hot = h; toast_render(); }
        TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (s_hot != -1) { s_hot = -1; toast_render(); }
        return 0;

    case WM_LBUTTONDOWN: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        s_pressed = PtInRect(&s_btnSnooze, p) ? 0
                  : (PtInRect(&s_btnDismiss, p) ? 1 : -1);
        toast_render();
        return 0;
    }

    case WM_LBUTTONUP: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hit = PtInRect(&s_btnSnooze, p) ? 0
                : (PtInRect(&s_btnDismiss, p) ? 1 : -1);
        int was = s_pressed;
        s_pressed = -1;

        if (hit == 0 && was == 0) {
            KillTimer(hwnd, T_AUTO);
            SetTimer(hwnd, T_SNOOZE, g_cfg.snoozeMinutes * 60 * 1000, NULL);
            toast_begin_close();
        } else if (hit == 1 && was == 1) {
            KillTimer(hwnd, T_AUTO);
            toast_begin_close();
        } else if (hit == -1 && was == -1) {
            toast_begin_close();          /* clicking the card opens the app */
            mainwnd_show();
        } else {
            toast_render();
        }
        return 0;
    }

    case WM_NCHITTEST:
        return HTCLIENT;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void toast_register(HINSTANCE hi)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.cbSize        = sizeof wc;
    wc.lpfnWndProc   = toast_proc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = TOAST_CLASS;
    RegisterClassExW(&wc);
}

void toast_show(const Event *ev)
{
    if (!ev) return;
    s_ev = *ev;
    s_haveEv = 1;

    if (!s_toast) {
        s_toast = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            TOAST_CLASS, L"", WS_POPUP,
            0, 0, SC(TW), SC(TH), NULL, NULL, g_inst, NULL);
        if (!s_toast) return;
    }

    toast_layout();
    s_closing = 0;
    s_hot = s_pressed = -1;
    s_anim = 0;

    ShowWindow(s_toast, SW_SHOWNOACTIVATE);
    SetWindowPos(s_toast, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    toast_render();
    SetTimer(s_toast, T_ANIM, 15, NULL);

    KillTimer(s_toast, T_AUTO);
    if (g_cfg.toastSeconds > 0)
        SetTimer(s_toast, T_AUTO, g_cfg.toastSeconds * 1000, NULL);

    play_alert();
}

void toast_hide(void)
{
    if (s_toast && IsWindowVisible(s_toast)) toast_begin_close();
}
