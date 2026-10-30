#include <lib/services.h>
#include <shims/win32/handles.h>
#include <shims/win32/teb.h>
#include <shims/win32/windows.h>
void *WINAPI RtlAllocateHeap(HANDLE, ULONG, SIZE_T);
BOOL WINAPI RtlFreeHeap(HANDLE, ULONG, void *);
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
struct thread_start {
  LPTHREAD_START_ROUTINE function;
  void *arg;
  TEB *teb;
  void *stack;
  SIZE_T stack_size;
  int64_t pid;
  DWORD code;
  unsigned done, joined;
};
void GaiaThreadEntry(void);
void WINAPI ExitThread(DWORD code) {
  TEB *t = NtCurrentTeb();
  struct thread_start *r = t->ArbitraryUserPointer;
  if (r) {
    r->code = code;
    __atomic_store_n(&r->done, 1, __ATOMIC_RELEASE);
  }
  gaia_call(60, code, 0, 0, 0, 0, 0);
  for (;;) {
  }
}
void GaiaThreadStart(struct thread_start *r) {
  gaia_call(158, 0x1001, (int64_t)(uintptr_t)r->teb, 0, 0, 0, 0);
  r->teb->ThreadId = gaia_tid();
  ExitThread(r->function(r->arg));
}
HANDLE WINAPI CreateThread(void *security, SIZE_T size,
                           LPTHREAD_START_ROUTINE fn, void *arg, DWORD flags,
                           DWORD *id) {
  if (security || flags || !fn || size > 16 * 1024 * 1024) {
    SetLastError(87);
    return 0;
  }
  if (size < 65536)
    size = 65536;
  size = (size + 4095) & ~4095ULL;
  struct thread_start *r = RtlAllocateHeap((HANDLE)1, 8, sizeof(*r));
  if (!r)
    return 0;
  int64_t stack = gaia_map(0, size, 3), teb = gaia_map(0, 8192, 3);
  if (stack < 0 || teb < 0) {
    if (stack > 0)
      gaia_unmap((void *)(uintptr_t)stack, size);
    if (teb > 0)
      gaia_unmap((void *)(uintptr_t)teb, 8192);
    RtlFreeHeap((HANDLE)1, 0, r);
    SetLastError(8);
    return 0;
  }
  r->function = fn;
  r->arg = arg;
  r->teb = (void *)(uintptr_t)teb;
  r->stack = (void *)(uintptr_t)stack;
  r->stack_size = size;
  TEB *t = r->teb;
  t->Self = t;
  t->StackBase = (void *)(uintptr_t)(stack + size);
  t->StackLimit = r->stack;
  t->ProcessEnvironmentBlock = NtCurrentTeb()->ProcessEnvironmentBlock;
  t->ProcessId = gaia_pid();
  t->ArbitraryUserPointer = r;
  r->pid = gaia_call(1100, (int64_t)(uintptr_t)GaiaThreadEntry, stack + size,
                     (int64_t)(uintptr_t)r, 0, 0, 0);
  if (r->pid < 0) {
    gaia_unmap(r->stack, size);
    gaia_unmap(t, 8192);
    RtlFreeHeap((HANDLE)1, 0, r);
    SetLastError(8);
    return 0;
  }
  if (id)
    *id = r->pid;
  HANDLE h = GaiaHandleCreate(GH_THREAD, (int64_t)(uintptr_t)r, 0);
  return h == INVALID_HANDLE_VALUE ? 0 : h;
}
DWORD GaiaWaitEvent(HANDLE,DWORD);
DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD timeout) {
  if(GaiaHandleType(h)==GH_EVENT)return GaiaWaitEvent(h,timeout);
  int64_t value;
  if (!GaiaHandleLookup(h, GH_THREAD, &value, 0))
    return WAIT_FAILED;
  struct thread_start *r = (void *)(uintptr_t)value;
  uint64_t start = 0, now = 0;
  gaia_time(&start);
  while (!__atomic_load_n(&r->done, __ATOMIC_ACQUIRE)) {
    gaia_time(&now);
    if (timeout != INFINITE && (now - start) / 1000000 >= timeout)
      return WAIT_TIMEOUT;
    gaia_sleep(1000000);
  }
  if (!__atomic_exchange_n(&r->joined, 1, __ATOMIC_ACQ_REL)) {
    gaia_call(1102, r->pid, 0, 0, 0, 0, 0);
    gaia_unmap(r->stack, r->stack_size);
    gaia_unmap(r->teb, 8192);
  }
  return WAIT_OBJECT_0;
}

void GaiaCloseThreadHandle(int64_t value){struct thread_start *r=(void *)(uintptr_t)value;if(__atomic_load_n(&r->done,__ATOMIC_ACQUIRE)&&__atomic_load_n(&r->joined,__ATOMIC_ACQUIRE))RtlFreeHeap((HANDLE)1,0,r);}
