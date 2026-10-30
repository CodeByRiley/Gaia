#include <lib/services.h>
#include <shims/win32/handles.h>
#include <shims/win32/windows.h>
static int descriptor(HANDLE h) {
  uintptr_t n = (uintptr_t)h;
  if (n >= 0x10 && n <= 0x12)
    return n - 0x10;
  int64_t value;
  if (!GaiaHandleLookup(h, GH_FILE, &value, 0))
    return -1;
  return (int)value;
}
HANDLE WINAPI GetStdHandle(DWORD which) {
  switch (which) {
  case STD_INPUT_HANDLE:
    return (HANDLE)0x10;
  case STD_OUTPUT_HANDLE:
    return (HANDLE)0x11;
  case STD_ERROR_HANDLE:
    return (HANDLE)0x12;
  default:
    SetLastError(87);
    return INVALID_HANDLE_VALUE;
  }
}
BOOL WINAPI ReadFile(HANDLE h, void *p, DWORD n, DWORD *done, void *overlap) {
  if (done)
    *done = 0;
  if (overlap || (!p && n) || !done) {
    SetLastError(87);
    return FALSE;
  }
  int fd = descriptor(h);
  if (fd < 0)
    return FALSE;
  int64_t r = gaia_read(fd, p, n);
  if (r < 0) {
    SetLastError(GaiaError(r));
    return FALSE;
  }
  *done = (DWORD)r;
  return TRUE;
}
BOOL WINAPI WriteFile(HANDLE h, const void *p, DWORD n, DWORD *done,
                      void *overlap) {
  if (done)
    *done = 0;
  if (overlap || (!p && n) || !done) {
    SetLastError(87);
    return FALSE;
  }
  int fd = descriptor(h);
  if (fd < 0)
    return FALSE;
  int64_t r = gaia_write(fd, p, n);
  if (r < 0) {
    SetLastError(GaiaError(r));
    return FALSE;
  }
  *done = (DWORD)r;
  return TRUE;
}
BOOL WINAPI WriteConsoleA(HANDLE h, const void *p, DWORD n, DWORD *done,
                          void *reserved) {
  if (reserved) {
    SetLastError(87);
    return FALSE;
  }
  return WriteFile(h, p, n, done, 0);
}
BOOL WINAPI WriteConsoleW(HANDLE h, const void *p, DWORD n, DWORD *done,
                          void *reserved) {
  if (done)
    *done = 0;
  if (reserved || !p || !done) {
    SetLastError(87);
    return FALSE;
  }
  const WCHAR *w = p;
  DWORD at = 0;
  while (at < n) {
    WCHAR chunk[3] = {w[at], 0, 0};
    DWORD consumed = 1;
    if (chunk[0] >= 0xd800 && chunk[0] <= 0xdbff && at + 1 < n) {
      chunk[1] = w[at + 1];
      consumed = 2;
    }
    char utf8[8];
    int k = GaiaUtf8(chunk, utf8, sizeof(utf8));
    DWORD bytes;
    if (k < 0) {
      SetLastError(1113);
      return FALSE;
    }
    if (!WriteFile(h, utf8, k, &bytes, 0) || bytes != (DWORD)k)
      return FALSE;
    at += consumed;
    *done = at;
  }
  return TRUE;
}
HANDLE WINAPI CreateFileA(const char *name, DWORD access, DWORD sharing,
                          void *security, DWORD creation, DWORD flags,
                          HANDLE template) {
  if (security || template || (sharing & ~7u) ||
      (access & ~(GENERIC_READ | GENERIC_WRITE)) ||
      (flags & ~FILE_ATTRIBUTE_NORMAL)) {
    SetLastError(120);
    return INVALID_HANDLE_VALUE;
  }
  char path[1024];
  if (GaiaPath(name, path, sizeof(path))) {
    SetLastError(87);
    return INVALID_HANDLE_VALUE;
  }
  unsigned f = (access & GENERIC_READ ? GAIA_READ : 0) |
               (access & GENERIC_WRITE ? GAIA_WRITE : 0);
  if (!f) {
    SetLastError(87);
    return INVALID_HANDLE_VALUE;
  }
  switch (creation) {
  case CREATE_NEW:
    f |= GAIA_CREATE | GAIA_EXCLUSIVE;
    break;
  case CREATE_ALWAYS:
    f |= GAIA_CREATE | GAIA_TRUNCATE;
    break;
  case OPEN_EXISTING:
    break;
  case OPEN_ALWAYS:
    f |= GAIA_CREATE;
    break;
  case TRUNCATE_EXISTING:
    f |= GAIA_TRUNCATE;
    break;
  default:
    SetLastError(87);
    return INVALID_HANDLE_VALUE;
  }
  int64_t fd = gaia_open(path, f);
  if (fd < 0) {
    SetLastError(GaiaError(fd));
    return INVALID_HANDLE_VALUE;
  }
  HANDLE h = GaiaHandleCreate(GH_FILE, fd, 0);
  if (h == INVALID_HANDLE_VALUE)
    gaia_close(fd);
  else
    SetLastError(0);
  return h;
}
HANDLE WINAPI CreateFileW(const WCHAR *p, DWORD a, DWORD s, void *sec, DWORD c,
                          DWORD f, HANDLE t) {
  char name[1024];
  if (GaiaUtf8(p, name, sizeof(name)) < 0) {
    SetLastError(1113);
    return INVALID_HANDLE_VALUE;
  }
  return CreateFileA(name, a, s, sec, c, f, t);
}
BOOL WINAPI CloseHandle(HANDLE h) {
  uintptr_t n = (uintptr_t)h;
  if (n >= 0x10 && n <= 0x12) {
    int64_t r = gaia_close(n - 0x10);
    if (r < 0)
      SetLastError(GaiaError(r));
    return r == 0;
  }
  return GaiaHandleClose(h);
}
BOOL WINAPI SetFilePointerEx(HANDLE h, LARGE_INTEGER off, LARGE_INTEGER *out,
                             DWORD how) {
  if (how > 2) {
    SetLastError(87);
    return FALSE;
  }
  int fd = descriptor(h);
  if (fd < 0)
    return FALSE;
  int64_t r = gaia_seek(fd, off.QuadPart, how);
  if (r < 0) {
    SetLastError(GaiaError(r));
    return FALSE;
  }
  if (out)
    out->QuadPart = r;
  return TRUE;
}
BOOL WINAPI DeleteFileA(const char *name) {
  char path[1024];
  if (GaiaPath(name, path, sizeof(path))) {
    SetLastError(87);
    return FALSE;
  }
  int64_t r = gaia_call(87, (int64_t)(uintptr_t)path, 0, 0, 0, 0, 0);
  if (r < 0)
    SetLastError(GaiaError(r));
  return r == 0;
}
DWORD WINAPI GetFileType(HANDLE h) {
  int fd = descriptor(h);
  return fd < 0 ? 0 : fd < 3 ? 2 : 1;
}
BOOL WINAPI GetConsoleMode(HANDLE h, DWORD *mode) {
  if (!mode || descriptor(h) > 2) {
    SetLastError(6);
    return FALSE;
  }
  *mode = 0;
  return TRUE;
}
BOOL WINAPI FlushFileBuffers(HANDLE h) {
  if (descriptor(h) < 0)
    return FALSE;
  int64_t r = gaia_call(1130, 0, 0, 0, 0, 0, 0);
  if (r < 0)
    SetLastError(GaiaError(r));
  return r == 0;
}
