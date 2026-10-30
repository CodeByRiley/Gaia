#ifndef GAIA_WINBASE_H
#define GAIA_WINBASE_H
#include "winnt.h"
#define STD_INPUT_HANDLE ((DWORD) - 10)
#define STD_OUTPUT_HANDLE ((DWORD) - 11)
#define STD_ERROR_HANDLE ((DWORD) - 12)
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INFINITE 0xffffffffu
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define WAIT_FAILED 0xffffffffu
#define TLS_OUT_OF_INDEXES 0xffffffffu
DWORD WINAPI GetLastError(void);
void WINAPI SetLastError(DWORD);
HANDLE WINAPI GetStdHandle(DWORD);
BOOL WINAPI ReadFile(HANDLE, void *, DWORD, DWORD *, void *);
BOOL WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
BOOL WINAPI WriteConsoleA(HANDLE, const void *, DWORD, DWORD *, void *);
BOOL WINAPI WriteConsoleW(HANDLE, const void *, DWORD, DWORD *, void *);
HANDLE WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
HANDLE WINAPI CreateFileW(const WCHAR *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
BOOL WINAPI CloseHandle(HANDLE);
BOOL WINAPI SetFilePointerEx(HANDLE, LARGE_INTEGER, LARGE_INTEGER *, DWORD);
BOOL WINAPI DeleteFileA(const char *);
void *WINAPI VirtualAlloc(void *, SIZE_T, DWORD, DWORD);
BOOL WINAPI VirtualFree(void *, SIZE_T, DWORD);
BOOL WINAPI VirtualProtect(void *, SIZE_T, DWORD, DWORD *);
HANDLE WINAPI GetProcessHeap(void);
void *WINAPI HeapAlloc(HANDLE, DWORD, SIZE_T);
BOOL WINAPI HeapFree(HANDLE, DWORD, void *);
void *WINAPI HeapReAlloc(HANDLE, DWORD, void *, SIZE_T);
SIZE_T WINAPI HeapSize(HANDLE, DWORD, const void *);
void WINAPI ExitProcess(UINT) __attribute__((noreturn));
void WINAPI ExitThread(DWORD) __attribute__((noreturn));
DWORD WINAPI GetCurrentProcessId(void);
DWORD WINAPI GetCurrentThreadId(void);
HANDLE WINAPI GetCurrentProcess(void);
HANDLE WINAPI GetCurrentThread(void);
void WINAPI Sleep(DWORD);
DWORD WINAPI GetTickCount(void);
uint64_t WINAPI GetTickCount64(void);
BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *);
BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *);
DWORD WINAPI TlsAlloc(void);
BOOL WINAPI TlsFree(DWORD);
void *WINAPI TlsGetValue(DWORD);
BOOL WINAPI TlsSetValue(DWORD, void *);
HMODULE WINAPI LoadLibraryA(const char *);
HMODULE WINAPI LoadLibraryW(const WCHAR *);
HMODULE WINAPI GetModuleHandleA(const char *);
HMODULE WINAPI GetModuleHandleW(const WCHAR *);
FARPROC WINAPI GetProcAddress(HMODULE, const char *);
BOOL WINAPI FreeLibrary(HMODULE);
char *WINAPI GetCommandLineA(void);
WCHAR *WINAPI GetCommandLineW(void);
DWORD WINAPI GetEnvironmentVariableA(const char *, char *, DWORD);
BOOL WINAPI SetEnvironmentVariableA(const char *, const char *);
HANDLE WINAPI CreateThread(void *, SIZE_T, LPTHREAD_START_ROUTINE, void *, DWORD, DWORD *);
DWORD WINAPI WaitForSingleObject(HANDLE, DWORD);
HANDLE WINAPI CreateEventA(void *, BOOL, BOOL, const char *);
HANDLE WINAPI CreateEventW(void *, BOOL, BOOL, const WCHAR *);
BOOL WINAPI SetEvent(HANDLE);
BOOL WINAPI ResetEvent(HANDLE);
typedef struct {
  void *DebugInfo;
  LONG LockCount, RecursionCount;
  HANDLE OwningThread, LockSemaphore;
  ULONG_PTR SpinCount;
} CRITICAL_SECTION;
void WINAPI InitializeCriticalSection(CRITICAL_SECTION *);
void WINAPI DeleteCriticalSection(CRITICAL_SECTION *);
void WINAPI EnterCriticalSection(CRITICAL_SECTION *);
void WINAPI LeaveCriticalSection(CRITICAL_SECTION *);
BOOL WINAPI TryEnterCriticalSection(CRITICAL_SECTION *);
#endif
