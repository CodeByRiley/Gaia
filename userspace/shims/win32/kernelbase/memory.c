#include <lib/services.h>
#include <shims/win32/handles.h>
#include <shims/win32/windows.h>
void *WINAPI RtlAllocateHeap(HANDLE, ULONG, SIZE_T);
BOOL WINAPI RtlFreeHeap(HANDLE, ULONG, void *);
void *WINAPI RtlReAllocateHeap(HANDLE, ULONG, void *, SIZE_T);
SIZE_T WINAPI RtlSizeHeap(HANDLE, ULONG, const void *);
HANDLE WINAPI GetProcessHeap(void) { return (HANDLE)1; }
void *WINAPI HeapAlloc(HANDLE h, DWORD f, SIZE_T n) { return RtlAllocateHeap(h, f, n); }
BOOL WINAPI HeapFree(HANDLE h, DWORD f, void *p) { return RtlFreeHeap(h, f, p); }
void *WINAPI HeapReAlloc(HANDLE h, DWORD f, void *p, SIZE_T n) {
  return RtlReAllocateHeap(h, f, p, n);
}
SIZE_T WINAPI HeapSize(HANDLE h, DWORD f, const void *p) { return RtlSizeHeap(h, f, p); }
struct mapping {
  void *base;
  SIZE_T size;
  DWORD protection;
};
static struct mapping maps[128];
static unsigned protection(DWORD f) {
  switch (f) {
  case PAGE_READONLY:
    return 1;
  case PAGE_READWRITE:
    return 3;
  case PAGE_EXECUTE_READ:
    return 5;
  case PAGE_EXECUTE_READWRITE:
    return 7;
  default:
    return 0;
  }
}
void *WINAPI VirtualAlloc(void *at, SIZE_T n, DWORD type, DWORD flags) {
  unsigned p = protection(flags);
  if (!n || n > SIZE_MAX - 4095 || !p || !(type & MEM_COMMIT) ||
      (type & ~(MEM_COMMIT | MEM_RESERVE)) || (at && ((uintptr_t)at & 4095))) {
    SetLastError(87);
    return 0;
  }
  n             = (n + 4095) & ~4095ULL;
  unsigned slot = 128;
  for (unsigned i = 0; i < 128; i++)
    if (!maps[i].base) {
      slot = i;
      break;
    }
  if (slot == 128) {
    SetLastError(8);
    return 0;
  }
  int64_t r = gaia_map(at, n, p);
  if (r < 0) {
    SetLastError(GaiaError(r));
    return 0;
  }
  maps[slot] = (struct mapping){(void *)(uintptr_t)r, n, flags};
  return maps[slot].base;
}
BOOL WINAPI VirtualFree(void *p, SIZE_T n, DWORD type) {
  if (n || type != MEM_RELEASE) {
    SetLastError(87);
    return FALSE;
  }
  for (unsigned i = 0; i < 128; i++)
    if (maps[i].base == p) {
      int64_t r = gaia_unmap(p, maps[i].size);
      if (r < 0) {
        SetLastError(GaiaError(r));
        return FALSE;
      }
      maps[i].base = 0;
      return TRUE;
    }
  SetLastError(87);
  return FALSE;
}
BOOL WINAPI VirtualProtect(void *p, SIZE_T n, DWORD flags, DWORD *old) {
  unsigned prot = protection(flags);
  if (!prot || !old || !n) {
    SetLastError(87);
    return FALSE;
  }
  for (unsigned i = 0; i < 128; i++)
    if (maps[i].base == p && n <= maps[i].size) {
      int64_t r = gaia_protect(p, (n + 4095) & ~4095ULL, prot);
      if (r < 0) {
        SetLastError(GaiaError(r));
        return FALSE;
      }
      *old               = maps[i].protection;
      maps[i].protection = flags;
      return TRUE;
    }
  SetLastError(87);
  return FALSE;
}
