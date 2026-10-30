#include <lib/services.h>
#include <shims/win32/handles.h>
#include <shims/win32/teb.h>
#include <shims/win32/windows.h>
BOOL GaiaInitializeDlls(void);
void WINAPI RtlExitUserProcess(UINT);
void WINAPI ExitProcess(UINT code) {
  RtlExitUserProcess(code);
  for (;;) {
  }
}
DWORD WINAPI GetCurrentProcessId(void) { return (DWORD)gaia_pid(); }
DWORD WINAPI GetCurrentThreadId(void) { return (DWORD)gaia_tid(); }
HANDLE WINAPI GetCurrentProcess(void) { return (HANDLE)(intptr_t)-1; }
HANDLE WINAPI GetCurrentThread(void) { return (HANDLE)(intptr_t)-2; }
void WINAPI Sleep(DWORD ms) {
  if (!ms)
    gaia_yield();
  else
    gaia_sleep((uint64_t)ms * 1000000);
}
uint64_t WINAPI GetTickCount64(void) {
  uint64_t ns = 0;
  gaia_time(&ns);
  return ns / 1000000;
}
DWORD WINAPI GetTickCount(void) { return (DWORD)GetTickCount64(); }
BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *out) {
  uint64_t ns;
  if (!out || gaia_time(&ns) < 0) {
    SetLastError(87);
    return FALSE;
  }
  out->QuadPart = ns;
  return TRUE;
}
BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *out) {
  if (!out) {
    SetLastError(87);
    return FALSE;
  }
  out->QuadPart = 1000000000;
  return TRUE;
}
HMODULE WINAPI GetModuleHandleA(const char *name) {
  int64_t r = gaia_call(1150, (int64_t)(uintptr_t)name, 0, 0, 0, 0, 0);
  if (r <= 0) {
    SetLastError(126);
    return 0;
  }
  return (void *)(uintptr_t)r;
}
HMODULE WINAPI GetModuleHandleW(const WCHAR *name) {
  if (!name)
    return GetModuleHandleA(0);
  char out[256];
  if (GaiaUtf8(name, out, sizeof(out)) < 0) {
    SetLastError(1113);
    return 0;
  }
  return GetModuleHandleA(out);
}
HMODULE WINAPI LoadLibraryA(const char *name) {
  if (!name) {
    SetLastError(87);
    return 0;
  }
  int64_t r = gaia_call(1150, (int64_t)(uintptr_t)name, 1, 0, 0, 0, 0);
  if (r <= 0) {
    SetLastError(126);
    return 0;
  }
  if (!GaiaInitializeDlls()) {
    SetLastError(1114);
    return 0;
  }
  return (void *)(uintptr_t)r;
}
HMODULE WINAPI LoadLibraryW(const WCHAR *name) {
  char out[256];
  if (GaiaUtf8(name, out, sizeof(out)) < 0) {
    SetLastError(1113);
    return 0;
  }
  return LoadLibraryA(out);
}
FARPROC WINAPI GetProcAddress(HMODULE module, const char *name) {
  uintptr_t n = (uintptr_t)name;
  int64_t r = gaia_call(1151, (int64_t)(uintptr_t)module, n <= 65535 ? 0 : n,
                        n <= 65535 ? n : 0, 0, 0, 0);
  if (r <= 0) {
    SetLastError(127);
    return 0;
  }
  return (FARPROC)(uintptr_t)r;
}
/* Modules are pinned until process teardown; unloading live code is
 * unsupported. */
BOOL WINAPI FreeLibrary(HMODULE h) {
  (void)h;
  SetLastError(120);
  return FALSE;
}
char *WINAPI GetCommandLineA(void) {
  return NtCurrentTeb()->ProcessEnvironmentBlock->GaiaBoot->command_line;
}
WCHAR *WINAPI GetCommandLineW(void) {
  static WCHAR line[2048];
  if (GaiaUtf16(GetCommandLineA(), line, 2048) < 0)
    line[0] = 0;
  return line;
}
struct env {
  char name[64], value[512];
};
static struct env env[16];
static int equal(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}
DWORD WINAPI GetEnvironmentVariableA(const char *name, char *out, DWORD cap) {
  if (!name) {
    SetLastError(87);
    return 0;
  }
  for (unsigned i = 0; i < 16; i++)
    if (equal(name, env[i].name)) {
      DWORD n = 0;
      while (env[i].value[n])
        n++;
      if (n >= cap)
        return n + 1;
      if (!out) {
        SetLastError(87);
        return 0;
      }
      for (DWORD k = 0; k <= n; k++)
        out[k] = env[i].value[k];
      return n;
    }
  SetLastError(203);
  return 0;
}
BOOL WINAPI SetEnvironmentVariableA(const char *name, const char *value) {
  if (!name || !*name) {
    SetLastError(87);
    return FALSE;
  }
  size_t n = 0, v = 0;
  while (name[n]) {
    if (name[n] == '=' || ++n >= 64) {
      SetLastError(87);
      return FALSE;
    }
  }
  if (value)
    while (value[v])
      if (++v >= 512) {
        SetLastError(87);
        return FALSE;
      }
  int slot = -1;
  for (int i = 0; i < 16; i++) {
    if (equal(name, env[i].name)) {
      slot = i;
      break;
    }
    if (slot < 0 && !env[i].name[0])
      slot = i;
  }
  if (slot < 0) {
    SetLastError(8);
    return FALSE;
  }
  if (!value) {
    env[slot].name[0] = 0;
    return TRUE;
  }
  for (size_t k = 0; k <= n; k++)
    env[slot].name[k] = name[k];
  for (size_t k = 0; k <= v; k++)
    env[slot].value[k] = value[k];
  return TRUE;
}
