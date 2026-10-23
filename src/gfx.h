#ifndef GFX_H
#define GFX_H
#include "app.h"

/* ---- palette (dark) --------------------------------------------------- */
#define C_BG        RGB(0x10, 0x13, 0x17)
#define C_CARD      RGB(0x19, 0x1D, 0x23)
#define C_CARD_HI   RGB(0x21, 0x27, 0x2F)
#define C_LINE      RGB(0x28, 0x2E, 0x37)
#define C_TEXT      RGB(0xE9, 0xED, 0xF2)
#define C_DIM       RGB(0x8C, 0x97, 0xA5)
#define C_FAINT     RGB(0x5B, 0x65, 0x72)
#define C_ACCENT    (g_cfg.accent)          /* [masjid] accent in config.ini */
#define C_ACCENT_DK RGB(0x3A, 0x44, 0x18)
#define C_WARN      RGB(0xE0, 0x8B, 0x3E)
#define C_BAD       RGB(0xD9, 0x5F, 0x5F)

/* ---- dpi -------------------------------------------------------------- */
extern int g_dpi;
#define SC(x) MulDiv((x), g_dpi, 96)

/* ---- fonts ------------------------------------------------------------ */
#define FW_REG  400
#define FW_MED  600
#define FW_BLD  700
HFONT gfx_font(int pt, int weight);
void  gfx_reset_fonts(void);

/* ---- primitives ------------------------------------------------------- */
void gfx_fill(HDC dc, RECT r, COLORREF c);
void gfx_round_rect(HDC dc, RECT r, int radius, COLORREF fill, COLORREF bg);
void gfx_round_outline(HDC dc, RECT r, int radius, COLORREF line, COLORREF bg);
void gfx_circle(HDC dc, int cx, int cy, int radius, COLORREF fill, COLORREF bg);

int  gfx_text(HDC dc, const wchar_t *s, RECT r, HFONT f, COLORREF c, UINT flags);
int  gfx_text_w(HDC dc, const wchar_t *s, HFONT f);   /* measured width */

/* ---- widgets ---------------------------------------------------------- */
void gfx_toggle(HDC dc, RECT r, int on, int hot, COLORREF bg);
void gfx_button(HDC dc, RECT r, const wchar_t *label, int hot, int pressed,
                int primary, COLORREF bg);

HICON    gfx_make_icon(int size);   /* crescent mark, drawn at runtime */
COLORREF gfx_shade(COLORREF c, double f);   /* f < 1 darkens, > 1 lightens */
COLORREF gfx_on_accent(void);               /* readable text over C_ACCENT  */

#endif
