#include "gfx.h"
#include <math.h>

int g_dpi = 96;

/* ---- fonts ------------------------------------------------------------ */

#define FONT_CACHE 16
static struct { int pt, weight; HFONT f; } s_fonts[FONT_CACHE];

HFONT gfx_font(int pt, int weight)
{
    for (int i = 0; i < FONT_CACHE; i++)
        if (s_fonts[i].f && s_fonts[i].pt == pt && s_fonts[i].weight == weight)
            return s_fonts[i].f;

    LOGFONTW lf;
    ZeroMemory(&lf, sizeof lf);
    lf.lfHeight         = -MulDiv(pt, g_dpi, 72);
    lf.lfWeight         = weight;
    lf.lfCharSet        = DEFAULT_CHARSET;
    lf.lfQuality        = CLEARTYPE_QUALITY;
    lf.lfOutPrecision   = OUT_TT_PRECIS;
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
    wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Segoe UI");

    HFONT f = CreateFontIndirectW(&lf);
    for (int i = 0; i < FONT_CACHE; i++)
        if (!s_fonts[i].f) {
            s_fonts[i].pt = pt; s_fonts[i].weight = weight; s_fonts[i].f = f;
            break;
        }
    return f;
}

void gfx_reset_fonts(void)
{
    for (int i = 0; i < FONT_CACHE; i++)
        if (s_fonts[i].f) { DeleteObject(s_fonts[i].f); s_fonts[i].f = NULL; }
}

/* ---- primitives ------------------------------------------------------- */

void gfx_fill(HDC dc, RECT r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

/* Rounded shapes are drawn 3x oversized and shrunk with HALFTONE, which
 * buys antialiased edges without pulling in GDI+. */
static HDC aa_begin(RECT r, int ss, COLORREF bg, HBITMAP *outBmp, HBITMAP *outOld)
{
    int w = (r.right - r.left) * ss, h = (r.bottom - r.top) * ss;
    if (w <= 0 || h <= 0 || w > 8000 || h > 8000) return NULL;

    HDC     dc  = CreateCompatibleDC(NULL);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) { DeleteDC(dc); return NULL; }

    *outOld = (HBITMAP)SelectObject(dc, bmp);
    *outBmp = bmp;

    RECT full = { 0, 0, w, h };
    gfx_fill(dc, full, bg);
    return dc;
}

static void aa_end(HDC dst, RECT r, int ss, HDC src, HBITMAP bmp, HBITMAP old)
{
    int prev = SetStretchBltMode(dst, HALFTONE);
    SetBrushOrgEx(dst, 0, 0, NULL);
    StretchBlt(dst, r.left, r.top, r.right - r.left, r.bottom - r.top,
               src, 0, 0, (r.right - r.left) * ss, (r.bottom - r.top) * ss, SRCCOPY);
    SetStretchBltMode(dst, prev);
    SelectObject(src, old);
    DeleteObject(bmp);
    DeleteDC(src);
}

void gfx_round_rect(HDC dc, RECT r, int radius, COLORREF fill, COLORREF bg)
{
    const int ss = 3;
    HBITMAP bmp, old;
    HDC m = aa_begin(r, ss, bg, &bmp, &old);
    if (!m) { gfx_fill(dc, r, fill); return; }

    HBRUSH br = CreateSolidBrush(fill);
    HPEN   pn = CreatePen(PS_SOLID, 1, fill);
    HBRUSH ob = (HBRUSH)SelectObject(m, br);
    HPEN   op = (HPEN)SelectObject(m, pn);

    int w = (r.right - r.left) * ss, h = (r.bottom - r.top) * ss;
    RoundRect(m, 0, 0, w + 1, h + 1, radius * 2 * ss, radius * 2 * ss);

    SelectObject(m, ob); SelectObject(m, op);
    DeleteObject(br); DeleteObject(pn);
    aa_end(dc, r, ss, m, bmp, old);
}

void gfx_round_outline(HDC dc, RECT r, int radius, COLORREF line, COLORREF bg)
{
    const int ss = 3;
    HBITMAP bmp, old;
    HDC m = aa_begin(r, ss, bg, &bmp, &old);
    if (!m) return;

    HBRUSH br = (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN   pn = CreatePen(PS_SOLID, ss, line);
    HBRUSH ob = (HBRUSH)SelectObject(m, br);
    HPEN   op = (HPEN)SelectObject(m, pn);

    int w = (r.right - r.left) * ss, h = (r.bottom - r.top) * ss;
    RoundRect(m, ss / 2, ss / 2, w - ss / 2, h - ss / 2,
              radius * 2 * ss, radius * 2 * ss);

    SelectObject(m, ob); SelectObject(m, op);
    DeleteObject(pn);
    aa_end(dc, r, ss, m, bmp, old);
}

void gfx_circle(HDC dc, int cx, int cy, int radius, COLORREF fill, COLORREF bg)
{
    RECT r = { cx - radius, cy - radius, cx + radius, cy + radius };
    const int ss = 4;
    HBITMAP bmp, old;
    HDC m = aa_begin(r, ss, bg, &bmp, &old);
    if (!m) return;

    HBRUSH br = CreateSolidBrush(fill);
    HPEN   pn = CreatePen(PS_SOLID, 1, fill);
    HBRUSH ob = (HBRUSH)SelectObject(m, br);
    HPEN   op = (HPEN)SelectObject(m, pn);

    int w = (r.right - r.left) * ss, h = (r.bottom - r.top) * ss;
    Ellipse(m, 0, 0, w, h);

    SelectObject(m, ob); SelectObject(m, op);
    DeleteObject(br); DeleteObject(pn);
    aa_end(dc, r, ss, m, bmp, old);
}

/* ---- text ------------------------------------------------------------- */

int gfx_text(HDC dc, const wchar_t *s, RECT r, HFONT f, COLORREF c, UINT flags)
{
    HFONT of = (HFONT)SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    int h = DrawTextW(dc, s, -1, &r, flags);
    SelectObject(dc, of);
    return h;
}

int gfx_text_w(HDC dc, const wchar_t *s, HFONT f)
{
    HFONT of = (HFONT)SelectObject(dc, f);
    SIZE sz;
    GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
    SelectObject(dc, of);
    return sz.cx;
}

/* ---- widgets ---------------------------------------------------------- */

void gfx_toggle(HDC dc, RECT r, int on, int hot, COLORREF bg)
{
    int h   = r.bottom - r.top;
    int rad = h / 2;
    COLORREF track = on ? C_ACCENT : (hot ? C_LINE : RGB(0x35, 0x3C, 0x46));
    gfx_round_rect(dc, r, rad, track, bg);

    int knobR = (h - SC(6)) / 2;
    int cx    = on ? r.right - rad : r.left + rad;
    gfx_circle(dc, cx, r.top + h / 2, knobR,
               on ? RGB(0x14, 0x18, 0x0A) : RGB(0xD2, 0xD8, 0xE0), track);
}

void gfx_button(HDC dc, RECT r, const wchar_t *label, int hot, int pressed,
                int primary, COLORREF bg)
{
    COLORREF fill, text;
    if (primary) {
        fill = pressed ? gfx_shade(C_ACCENT, 0.83) : (hot ? gfx_shade(C_ACCENT, 1.09) : C_ACCENT);
        text = gfx_on_accent();
    } else {
        fill = pressed ? RGB(0x2A, 0x31, 0x3A) : (hot ? C_CARD_HI : C_CARD);
        text = C_TEXT;
    }
    gfx_round_rect(dc, r, SC(7), fill, bg);
    if (!primary) gfx_round_outline(dc, r, SC(7), C_LINE, fill);

    RECT t = r;
    t.top += (r.bottom - r.top - SC(17)) / 2;
    gfx_text(dc, label, t, gfx_font(10, FW_MED), text,
             DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
}

/* ---- app icon --------------------------------------------------------- */
/* A crescent, rasterised with 4x4 coverage sampling so it stays clean at
 * 16px in the tray. Premultiplied BGRA, which is what DrawIconEx blends. */

COLORREF gfx_shade(COLORREF c, double f)
{
    int r = (int)(GetRValue(c) * f + 0.5), g = (int)(GetGValue(c) * f + 0.5),
        b = (int)(GetBValue(c) * f + 0.5);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

COLORREF gfx_on_accent(void)
{
    /* Relative luminance; dark ink on light accents, light ink on dark. */
    double l = 0.2126 * GetRValue(C_ACCENT) + 0.7152 * GetGValue(C_ACCENT)
             + 0.0722 * GetBValue(C_ACCENT);
    return l > 140 ? RGB(0x12, 0x16, 0x08) : RGB(0xF4, 0xF6, 0xF8);
}

HICON gfx_make_icon(int size)
{
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = size;
    bi.bmiHeader.biHeight      = -size;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC     dc   = CreateCompatibleDC(NULL);
    BYTE   *bits = NULL;
    HBITMAP col  = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (!col) { DeleteDC(dc); return NULL; }

    double s   = size;
    double ocx = 0.50 * s, ocy = 0.50 * s, orad = 0.45 * s;
    double icx = 0.66 * s, icy = 0.38 * s, irad = 0.38 * s;

    const double R = GetRValue(C_ACCENT), G = GetGValue(C_ACCENT), B = GetBValue(C_ACCENT);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int hits = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    double px = x + (sx + 0.5) / 4.0;
                    double py = y + (sy + 0.5) / 4.0;
                    double d1 = hypot(px - ocx, py - ocy);
                    double d2 = hypot(px - icx, py - icy);
                    if (d1 <= orad && d2 > irad) hits++;
                }
            }
            double a = hits / 16.0;
            BYTE  *p = bits + (y * size + x) * 4;
            p[0] = (BYTE)(B * a + 0.5);      /* premultiplied */
            p[1] = (BYTE)(G * a + 0.5);
            p[2] = (BYTE)(R * a + 0.5);
            p[3] = (BYTE)(a * 255.0 + 0.5);
        }
    }

    HBITMAP mask = CreateBitmap(size, size, 1, 1, NULL);
    ICONINFO ii;
    ZeroMemory(&ii, sizeof ii);
    ii.fIcon    = TRUE;
    ii.hbmColor = col;
    ii.hbmMask  = mask;

    HICON ic = CreateIconIndirect(&ii);
    DeleteObject(col);
    DeleteObject(mask);
    DeleteDC(dc);
    return ic;
}
