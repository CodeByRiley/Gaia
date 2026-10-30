#include <shims/win32/handles.h>
#include <shims/win32/teb.h>
#include <shims/win32/windows.h>
DWORD WINAPI GetLastError(void) { return NtCurrentTeb()->LastErrorValue; }
void WINAPI SetLastError(DWORD e) { NtCurrentTeb()->LastErrorValue = e; }
DWORD GaiaError(int64_t result) {
  switch (-result) {
  case 0:
    return 0;
  case 2:
    return 2;
  case 9:
    return 6;
  case 12:
    return 8;
  case 13:
  case 1:
    return 5;
  case 17:
    return 183;
  case 20:
    return 3;
  case 38:
    return 120;
  default:
    return 87;
  }
}
