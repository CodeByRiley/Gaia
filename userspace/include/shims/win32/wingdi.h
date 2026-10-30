#ifndef GAIA_WINGDI_H
#define GAIA_WINGDI_H
#include "windef.h"
#define BLACK_BRUSH 4
#define WHITE_BRUSH 0
#define TRANSPARENT 1
#define SRCCOPY 0x00cc0020
HBRUSH WINAPI CreateSolidBrush(COLORREF);
BOOL WINAPI DeleteObject(HGDIOBJ);
HGDIOBJ WINAPI GetStockObject(INT);
COLORREF WINAPI SetPixel(HDC, INT, INT, COLORREF);
BOOL WINAPI Rectangle(HDC, INT, INT, INT, INT);
BOOL WINAPI TextOutA(HDC, INT, INT, const char *, INT);
BOOL WINAPI TextOutW(HDC, INT, INT, const WCHAR *, INT);
COLORREF WINAPI SetTextColor(HDC, COLORREF);
COLORREF WINAPI SetBkColor(HDC, COLORREF);
INT WINAPI SetBkMode(HDC, INT);
typedef struct {
  DWORD biSize;
  LONG biWidth, biHeight;
  WORD biPlanes, biBitCount;
  DWORD biCompression, biSizeImage;
  LONG biXPelsPerMeter, biYPelsPerMeter;
  DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER;
typedef struct {
  BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved;
} RGBQUAD;
typedef struct {
  BITMAPINFOHEADER bmiHeader;
  RGBQUAD bmiColors[1];
} BITMAPINFO;
INT WINAPI SetDIBitsToDevice(HDC, INT, INT, DWORD, DWORD, INT, INT, UINT, UINT, const void *,
                             const BITMAPINFO *, UINT);
#endif
