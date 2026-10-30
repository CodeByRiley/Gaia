/* A Windows SDK program: no Gaia headers, syscalls, or special startup code.
 * It is compiled once with the ordinary MinGW import library, then packaged. */
#include <windows.h>
static int failed;
static void report(const char *s) {
  DWORD n = 0;
  const char *p = s;
  while (*p)
    p++;
  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)(p - s), &n, NULL);
}
static void check(BOOL ok, const char *name) {
  if (!ok) {
    failed++;
    report("winhello: FAIL ");
    report(name);
    report("\n");
  }
}
static DWORD WINAPI worker(void *p) {
  DWORD slot = *(DWORD *)p;
  check(TlsGetValue(slot) == NULL, "new thread TLS");
  TlsSetValue(slot, (void *)456);
  SetLastError(777);
  Sleep(5);
  check(GetLastError() == 777, "thread last error");
  return 42;
}
void mainCRTStartup(void) {
  report("winhello: ordinary Windows imports resolved\n");
  SetLastError(123);
  Sleep(1);
  check(GetLastError() == 123, "last error survives syscall");
  HANDLE file = CreateFileA("C:\\win32-test.txt", GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  check(file != INVALID_HANDLE_VALUE, "CreateFileA");
  if (file != INVALID_HANDLE_VALUE) {
    DWORD n;
    char data[5] = {0};
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    check(WriteFile(file, "Gaia", 4, &n, NULL) && n == 4, "WriteFile");
    check(SetFilePointerEx(file, zero, NULL, FILE_BEGIN), "seek");
    check(ReadFile(file, data, 4, &n, NULL) && n == 4 && data[0] == 'G' &&
              data[3] == 'a',
          "ReadFile");
    check(CloseHandle(file), "close");
    check(!CloseHandle(file) && GetLastError() == ERROR_INVALID_HANDLE,
          "stale handle rejected");
  }
  void *p = VirtualAlloc(NULL, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  check(p != NULL, "VirtualAlloc");
  if (p) {
    *(volatile int *)p = 123;
    check(VirtualFree(p, 0, MEM_RELEASE), "VirtualFree");
  }
  p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 64);
  check(p && *(BYTE *)p == 0, "HeapAlloc");
  if (p)
    check(HeapFree(GetProcessHeap(), 0, p), "HeapFree");
  DWORD slot = TlsAlloc();
  check(slot != TLS_OUT_OF_INDEXES, "TlsAlloc");
  TlsSetValue(slot, (void *)123);
  HANDLE thread = CreateThread(NULL, 0, worker, &slot, 0, NULL);
  check(thread != NULL, "CreateThread");
  if (thread) {
    check(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0, "thread wait");
    CloseHandle(thread);
  }
  check(TlsGetValue(slot) == (void *)123, "parent TLS isolation");
  TlsFree(slot);
  HMODULE dll = LoadLibraryA("msvcrt.dll");
  check(dll != NULL, "dynamic DLL loading");
  if (dll) {
    typedef size_t(__cdecl * length_fn)(const char *);
    length_fn length = (length_fn)GetProcAddress(dll, "strlen");
    check(length && length("unchanged") == 9, "GetProcAddress");
  }
  check(GetProcAddress(GetModuleHandleA("kernel32.dll"), "MissingApi") ==
                NULL &&
            GetLastError() == ERROR_PROC_NOT_FOUND,
        "missing export");
  HANDLE event=CreateEventA(NULL,FALSE,TRUE,NULL);
  check(event && WaitForSingleObject(event,0)==WAIT_OBJECT_0 && WaitForSingleObject(event,0)==WAIT_TIMEOUT,"auto-reset event");
  if(event){check(SetEvent(event)&&WaitForSingleObject(event,1)==WAIT_OBJECT_0,"event signaling");CloseHandle(event);}
  CRITICAL_SECTION critical;InitializeCriticalSection(&critical);EnterCriticalSection(&critical);check(TryEnterCriticalSection(&critical),"recursive critical section");LeaveCriticalSection(&critical);LeaveCriticalSection(&critical);DeleteCriticalSection(&critical);
  HMODULE probe = LoadLibraryA("compatprobe.dll");
  if (probe) {
    typedef int (WINAPI *probe_fn)(void);
    union { FARPROC raw; probe_fn call; } named, ordinal;
    named.raw = GetProcAddress(probe, "ProbeValue");
    ordinal.raw = GetProcAddress(probe, (const char *)7);
    check(named.call && ordinal.call && named.call() == 42 && ordinal.call() == 42,
          "relocated DLLs, dependency initialization, name and ordinal exports");
    check(GetProcAddress(probe, "ProbeProcessId") != NULL, "forwarded export");
    report("winhello: DLL lifecycle checks passed\n");
  }
  if (!failed)
    report("winhello: all checks passed\n");
  ExitProcess(failed ? 1 : 0);
}
