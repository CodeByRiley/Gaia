#ifndef GAIA_WINDEF_H
#define GAIA_WINDEF_H
#include <stddef.h>
#include <stdint.h>
#define WINAPI __attribute__((ms_abi))
#define CALLBACK WINAPI
#define WINEXPORT __attribute__((dllexport))
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
typedef int32_t BOOL, LONG, INT;
typedef uint32_t DWORD, UINT, ULONG, COLORREF;
typedef uint16_t WORD, WCHAR, ATOM;
typedef uint8_t BYTE;
typedef uint64_t ULONG_PTR, DWORD_PTR, SIZE_T, WPARAM;
typedef int64_t LONG_PTR, LPARAM, LRESULT;
typedef void *HANDLE, *HMODULE, *HINSTANCE, *HWND, *HDC, *HBRUSH, *HGDIOBJ, *HICON, *HCURSOR,
    *HMENU, *LPVOID;
typedef const void *LPCVOID;
typedef char CHAR, *LPSTR;
typedef const char *LPCSTR;
typedef WCHAR *LPWSTR;
typedef const WCHAR *LPCWSTR;
typedef DWORD *LPDWORD;
typedef BOOL *LPBOOL;
typedef struct {
  LONG x, y;
} POINT;
typedef struct {
  LONG left, top, right, bottom;
} RECT;
typedef union {
  struct {
    DWORD LowPart;
    LONG HighPart;
  };
  int64_t QuadPart;
} LARGE_INTEGER;
typedef struct {
  DWORD dwLowDateTime, dwHighDateTime;
} FILETIME;
typedef void(WINAPI *FARPROC)(void);
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define RGB(r, g, b) ((COLORREF)((BYTE)(r) | ((WORD)(BYTE)(g) << 8) | ((DWORD)(BYTE)(b) << 16)))
#endif
