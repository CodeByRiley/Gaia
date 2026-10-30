#ifndef GAIA_TEB_H
#define GAIA_TEB_H
#include "peb.h"
typedef struct {
  void *ExceptionList, *StackBase, *StackLimit, *SubSystemTib, *FiberData, *ArbitraryUserPointer;
  void *Self, *EnvironmentPointer;
  uint64_t ProcessId, ThreadId;
  void *ActiveRpcHandle, *ThreadLocalStoragePointer;
  PEB *ProcessEnvironmentBlock;
  DWORD LastErrorValue;
  BYTE reserved[0x1480 - 0x6c];
  void *TlsSlots[64];
} TEB;
_Static_assert(offsetof(TEB, Self) == 0x30, "TEB self");
_Static_assert(offsetof(TEB, ProcessEnvironmentBlock) == 0x60, "TEB PEB");
_Static_assert(offsetof(TEB, LastErrorValue) == 0x68, "TEB last error");
_Static_assert(offsetof(TEB, TlsSlots) == 0x1480, "TEB TLS slots");
static inline TEB *NtCurrentTeb(void) {
  TEB *t;
  __asm__("movq %%gs:0x30,%0" : "=r"(t));
  return t;
}
#endif
