#include <assert.h>
#include <shims/win32/handles.h>
#include <shims/win32/winerror.h>
#include <stdio.h>
#include <string.h>
DWORD GaiaError(int64_t r) { return (DWORD)-r; }
BOOL WINAPI RtlFreeHeap(HANDLE h,ULONG f,void *p){(void)h;(void)f;(void)p;return TRUE;}
void GaiaCloseThreadHandle(int64_t v){(void)v;}
static DWORD error;
static int closed;
void WINAPI SetLastError(DWORD e) { error = e; }
int64_t gaia_yield(void) { return 0; }
int64_t gaia_close(int fd) {
  closed = fd;
  return 0;
}
int main(void) {
  HANDLE a = GaiaHandleCreate(GH_FILE, 42, 0);
  int64_t value;
  assert(a != INVALID_HANDLE_VALUE && GaiaHandleLookup(a, GH_FILE, &value, 0) &&
         value == 42);
  assert(!GaiaHandleLookup(a, GH_THREAD, &value, 0) && error == 6);
  assert(GaiaHandleClose(a) && closed == 42);
  assert(!GaiaHandleClose(a));
  HANDLE b = GaiaHandleCreate(GH_FILE, 43, 0);
  assert(b != a && !GaiaHandleLookup(a, GH_FILE, &value, 0));
  assert(!GaiaHandleLookup((HANDLE)(uintptr_t)0xffffffffffffffffULL, 0, 0, 0));
  char path[128];
  assert(!GaiaPath("C:\\system\\file.txt", path, sizeof(path)) &&
         !strcmp(path, "/system/file.txt"));
  assert(GaiaPath("Z:\\file", path, sizeof(path)) < 0 &&
         GaiaPath("C:relative", path, sizeof(path)) < 0);
  WCHAR wide[] = {0x41, 0xd83d, 0xde00, 0};
  char utf8[16];
  WCHAR back[8];
  assert(GaiaUtf8(wide, utf8, sizeof(utf8)) == 5 &&
         GaiaUtf16(utf8, back, 8) == 3 && !memcmp(wide, back, sizeof(wide)));
  WCHAR broken[] = {0xd800, 0};
  assert(GaiaUtf8(broken, utf8, 16) < 0);
  assert(GaiaUtf16("\xc0\x80", back, 8) < 0);
  assert(GaiaUtf16("\xed\xa0\x80", back, 8) < 0);
  assert(GaiaUtf8(wide, utf8, 5) < 0 && GaiaUtf16("abc", back, 3) < 0);
  assert(GaiaHandleClose(b));
  puts("Win32 handle generations, path conversion and Unicode: passed");
  return 0;
}
