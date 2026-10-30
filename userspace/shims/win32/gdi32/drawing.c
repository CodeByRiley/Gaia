#include <shims/win32/bridge.h>
static struct {
  DWORD color;
  int used;
} brushes[64];
static DWORD argb(COLORREF c) {
  return 0xff000000 | ((c & 255) << 16) | (c & 0xff00) | ((c >> 16) & 255);
}
static int brush(HBRUSH b, DWORD *color) {
  uintptr_t n = (uintptr_t)b;
  if (n == 1) {
    *color = 0xffffffff;
    return 1;
  }
  if (n == 2) {
    *color = 0xff000000;
    return 1;
  }
  if (n >= 256 && n < 320 && brushes[n - 256].used) {
    *color = brushes[n - 256].color;
    return 1;
  }
  return 0;
}
HBRUSH WINAPI CreateSolidBrush(COLORREF c) {
  for (unsigned i = 0; i < 64; i++)
    if (!brushes[i].used) {
      brushes[i].color = argb(c);
      brushes[i].used  = 1;
      return (HBRUSH)(uintptr_t)(256 + i);
    }
  SetLastError(8);
  return 0;
}
BOOL WINAPI DeleteObject(HGDIOBJ b) {
  uintptr_t n = (uintptr_t)b;
  if (n >= 256 && n < 320 && brushes[n - 256].used) {
    brushes[n - 256].used = 0;
    return TRUE;
  }
  SetLastError(6);
  return FALSE;
}
HGDIOBJ WINAPI GetStockObject(INT index) {
  return index == WHITE_BRUSH ? (HGDIOBJ)1 : index == BLACK_BRUSH ? (HGDIOBJ)2 : 0;
}
INT WINAPI FillRect(HDC dc, const RECT *rect, HBRUSH b) {
  struct gaia_dc *d = GaiaGetDC(dc);
  DWORD color;
  if (!d || !rect || !brush(b, &color))
    return 0;
  LONG l = rect->left < 0 ? 0 : rect->left, t = rect->top < 0 ? 0 : rect->top,
       r   = rect->right > d->window.w ? d->window.w : rect->right,
       bot = rect->bottom > d->window.h ? d->window.h : rect->bottom;
  for (LONG y = t; y < bot; y++) {
    DWORD *row = (DWORD *)(uintptr_t)(d->window.surface_va + (uint64_t)y * d->window.pitch);
    for (LONG x = l; x < r; x++)
      row[x] = color;
  }
  return 1;
}
COLORREF WINAPI SetPixel(HDC dc, INT x, INT y, COLORREF c) {
  struct gaia_dc *d = GaiaGetDC(dc);
  if (!d || x < 0 || y < 0 || x >= d->window.w || y >= d->window.h)
    return 0xffffffffu;
  DWORD *p = (DWORD *)(uintptr_t)(d->window.surface_va + (uint64_t)y * d->window.pitch);
  p[x]     = argb(c);
  return c;
}
BOOL WINAPI Rectangle(HDC dc, INT l, INT t, INT r, INT b) {
  if (!GaiaGetDC(dc))
    return FALSE;
  for (INT x = l; x < r; x++) {
    SetPixel(dc, x, t, 0);
    SetPixel(dc, x, b - 1, 0);
  }
  for (INT y = t; y < b; y++) {
    SetPixel(dc, l, y, 0);
    SetPixel(dc, r - 1, y, 0);
  }
  return TRUE;
}
COLORREF WINAPI SetTextColor(HDC dc, COLORREF c) {
  struct gaia_dc *d = GaiaGetDC(dc);
  if (!d)
    return 0xffffffffu;
  DWORD old     = d->text_color;
  d->text_color = c;
  return old;
}
COLORREF WINAPI SetBkColor(HDC dc, COLORREF c) {
  struct gaia_dc *d = GaiaGetDC(dc);
  if (!d)
    return 0xffffffffu;
  DWORD old           = d->background_color;
  d->background_color = c;
  return old;
}
INT WINAPI SetBkMode(HDC dc, INT mode) {
  struct gaia_dc *d = GaiaGetDC(dc);
  if (!d || mode < 1 || mode > 2)
    return 0;
  INT old            = d->background_mode;
  d->background_mode = mode;
  return old;
}

#include <fonts/font8x8.h>
BOOL WINAPI TextOutA(HDC dc, INT x, INT y, const char *text, INT count) {
  struct gaia_dc *d = GaiaGetDC(dc);
  if (!d || !text || count < 0)
    return FALSE;
  for (INT i = 0; i < count; i++) {
    unsigned c = (unsigned char)text[i];
    if (c < FONT_FIRST || c > FONT_LAST)
      c = '?';
    for (INT row = 0; row < 8; row++)
      for (INT col = 0; col < 8; col++) {
        if (font8x8[c - FONT_FIRST][row] & (1u << col))
          SetPixel(dc, x + i * 8 + col, y + row, d->text_color);
        else if (d->background_mode != TRANSPARENT)
          SetPixel(dc, x + i * 8 + col, y + row, d->background_color);
      }
  }
  return TRUE;
}
BOOL WINAPI TextOutW(HDC dc, INT x, INT y, const WCHAR *text, INT count) {
  if (!text || count < 0)
    return FALSE;
  for (INT i = 0; i < count; i++) {
    char c = text[i] >= 32 && text[i] <= 126 ? (char)text[i] : '?';
    if (!TextOutA(dc, x + i * 8, y, &c, 1))
      return FALSE;
  }
  return TRUE;
}

INT WINAPI SetDIBitsToDevice(HDC dc, INT x, INT y, DWORD width, DWORD height, INT sx, INT sy,
                             UINT start, UINT lines, const void *bits, const BITMAPINFO *info,
                             UINT use) {
  struct gaia_dc *d = GaiaGetDC(dc);
  if (!d || !bits || !info)
    return 0;
  const BITMAPINFOHEADER *h = &info->bmiHeader;
  if (h->biSize < 40 || h->biPlanes != 1 || h->biBitCount != 32 || h->biCompression || use ||
      start || h->biWidth <= 0 || h->biWidth > 16384 || h->biHeight == 0 ||
      h->biHeight == INT32_MIN || sx < 0 || sy < 0 || width > 16384 || height > 16384) {
    SetLastError(120);
    return 0;
  }
  unsigned rows = h->biHeight < 0 ? -h->biHeight : h->biHeight;
  if (rows > 16384 || lines < rows || (uint64_t)sx + width > (unsigned)h->biWidth ||
      (uint64_t)sy + height > rows) {
    SetLastError(87);
    return 0;
  }
  const DWORD *source = bits;
  for (DWORD row = 0; row < height; row++) {
    int64_t dy = (int64_t)y + row;
    if (dy < 0 || dy >= d->window.h)
      continue;
    unsigned source_y = sy + row;
    if (h->biHeight > 0)
      source_y = rows - 1 - source_y;
    DWORD *dest = (DWORD *)(uintptr_t)(d->window.surface_va + (uint64_t)dy * d->window.pitch);
    for (DWORD col = 0; col < width; col++) {
      int64_t dx = (int64_t)x + col;
      if (dx >= 0 && dx < d->window.w)
        dest[dx] = 0xff000000 | source[(size_t)source_y * h->biWidth + sx + col];
    }
  }
  return height;
}
