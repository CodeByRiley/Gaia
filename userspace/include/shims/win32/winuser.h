#ifndef GAIA_WINUSER_H
#define GAIA_WINUSER_H
#include "windef.h"
typedef LRESULT(CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef struct {
  UINT style;
  WNDPROC lpfnWndProc;
  INT cbClsExtra, cbWndExtra;
  HINSTANCE hInstance;
  HICON hIcon;
  HCURSOR hCursor;
  HBRUSH hbrBackground;
  LPCSTR lpszMenuName, lpszClassName;
} WNDCLASSA;
typedef struct {
  UINT style;
  WNDPROC lpfnWndProc;
  INT cbClsExtra, cbWndExtra;
  HINSTANCE hInstance;
  HICON hIcon;
  HCURSOR hCursor;
  HBRUSH hbrBackground;
  LPCWSTR lpszMenuName, lpszClassName;
} WNDCLASSW;
typedef struct {
  UINT cbSize;
  UINT style;
  WNDPROC lpfnWndProc;
  INT cbClsExtra, cbWndExtra;
  HINSTANCE hInstance;
  HICON hIcon;
  HCURSOR hCursor;
  HBRUSH hbrBackground;
  LPCSTR lpszMenuName, lpszClassName;
  HICON hIconSm;
} WNDCLASSEXA;
typedef struct {
  HWND hwnd;
  UINT message;
  WPARAM wParam;
  LPARAM lParam;
  DWORD time;
  POINT pt;
  DWORD lPrivate;
} MSG;
typedef struct {
  HDC hdc;
  BOOL fErase;
  RECT rcPaint;
  BOOL fRestore, fIncUpdate;
  BYTE rgbReserved[32];
} PAINTSTRUCT;
typedef struct {
  void *lpCreateParams;
  HINSTANCE hInstance;
  HMENU hMenu;
  HWND hwndParent;
  INT cy, cx, y, x;
  LONG style;
  LPCSTR lpszName, lpszClass;
  DWORD dwExStyle;
} CREATESTRUCTA;
#define WM_CREATE 1
#define WM_DESTROY 2
#define WM_SIZE 5
#define WM_PAINT 15
#define WM_CLOSE 16
#define WM_QUIT 18
#define WM_ERASEBKGND 20
#define WM_NCCREATE 0x81
#define WM_NCDESTROY 0x82
#define WM_KEYDOWN 0x100
#define WM_KEYUP 0x101
#define WM_CHAR 0x102
#define WM_MOUSEMOVE 0x200
#define WM_LBUTTONDOWN 0x201
#define WM_LBUTTONUP 0x202
#define WS_OVERLAPPEDWINDOW 0x00cf0000u
#define WS_VISIBLE 0x10000000u
#define CW_USEDEFAULT ((INT)0x80000000)
#define SW_SHOW 5
#define PM_REMOVE 1
#define COLOR_WINDOW 5
ATOM WINAPI RegisterClassA(const WNDCLASSA *);
ATOM WINAPI RegisterClassW(const WNDCLASSW *);
ATOM WINAPI RegisterClassExA(const WNDCLASSEXA *);
HWND WINAPI CreateWindowExA(DWORD, const char *, const char *, DWORD, INT, INT,
                            INT, INT, HWND, HMENU, HINSTANCE, void *);
HWND WINAPI CreateWindowExW(DWORD, const WCHAR *, const WCHAR *, DWORD, INT,
                            INT, INT, INT, HWND, HMENU, HINSTANCE, void *);
BOOL WINAPI DestroyWindow(HWND);
BOOL WINAPI ShowWindow(HWND, INT);
BOOL WINAPI UpdateWindow(HWND);
BOOL WINAPI GetMessageA(MSG *, HWND, UINT, UINT);
BOOL WINAPI GetMessageW(MSG *, HWND, UINT, UINT);
BOOL WINAPI PeekMessageA(MSG *, HWND, UINT, UINT, UINT);
BOOL WINAPI PeekMessageW(MSG *, HWND, UINT, UINT, UINT);
BOOL WINAPI TranslateMessage(const MSG *);
LRESULT WINAPI DispatchMessageA(const MSG *);
LRESULT WINAPI DispatchMessageW(const MSG *);
LRESULT WINAPI DefWindowProcA(HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
BOOL WINAPI PostMessageA(HWND, UINT, WPARAM, LPARAM);
void WINAPI PostQuitMessage(INT);
HDC WINAPI BeginPaint(HWND, PAINTSTRUCT *);
BOOL WINAPI EndPaint(HWND, const PAINTSTRUCT *);
HDC WINAPI GetDC(HWND);
INT WINAPI ReleaseDC(HWND, HDC);
BOOL WINAPI GetClientRect(HWND, RECT *);
BOOL WINAPI InvalidateRect(HWND, const RECT *, BOOL);
BOOL WINAPI SetWindowTextA(HWND, const char *);
INT WINAPI FillRect(HDC, const RECT *, HBRUSH);
#endif
