#include <lib/services.h>
#include <lib/syscall.h>
#include <shims/win32/handles.h>
#include <shims/win32/teb.h>
#include <shims/win32/windows.h>
#define PTR(p) ((int64_t)(uintptr_t)(p))
void *memcpy(void *d, const void *s, size_t n) {
  BYTE *a       = d;
  const BYTE *b = s;
  for (size_t i = 0; i < n; i++)
    a[i] = b[i];
  return d;
}
void *memset(void *d, int c, size_t n) {
  BYTE *a = d;
  for (size_t i = 0; i < n; i++)
    a[i] = c;
  return d;
}
void *memmove(void *d, const void *s, size_t n) {
  BYTE *a       = d;
  const BYTE *b = s;
  if (a < b)
    for (size_t i = 0; i < n; i++)
      a[i] = b[i];
  else
    for (size_t i = n; i; i--)
      a[i - 1] = b[i - 1];
  return d;
}
int memcmp(const void *a, const void *b, size_t n) {
  const BYTE *x = a, *y = b;
  for (size_t i = 0; i < n; i++)
    if (x[i] != y[i])
      return x[i] - y[i];
  return 0;
}
size_t strlen(const char *s) {
  size_t n = 0;
  while (s[n])
    n++;
  return n;
}
int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (BYTE)*a - (BYTE)*b;
}
void WINAPI RtlMoveMemory(void *a, const void *b, SIZE_T n) { memmove(a, b, n); }
void WINAPI RtlZeroMemory(void *a, SIZE_T n) { memset(a, 0, n); }
struct allocation {
  uint64_t magic, size, mapped;
};
#define ALLOC_MAGIC 0x4741494148454150ULL
void *WINAPI RtlAllocateHeap(HANDLE heap, ULONG flags, SIZE_T n) {
  if (heap != (HANDLE)1 || flags & ~HEAP_ZERO_MEMORY ||
      n > SIZE_MAX - sizeof(struct allocation) - 4095) {
    SetLastError(87);
    return 0;
  }
  SIZE_T bytes = (n + sizeof(struct allocation) + 4095) & ~4095ULL;
  int64_t r    = gaia_map(0, bytes, 3);
  if (r < 0) {
    SetLastError(GaiaError(r));
    return 0;
  }
  struct allocation *a = (void *)(uintptr_t)r;
  a->magic             = ALLOC_MAGIC;
  a->size              = n;
  a->mapped            = bytes;
  return a + 1;
}
BOOL WINAPI RtlFreeHeap(HANDLE heap, ULONG flags, void *p) {
  if (!p)
    return TRUE;
  struct allocation *a = (struct allocation *)p - 1;
  if (heap != (HANDLE)1 || flags || a->magic != ALLOC_MAGIC) {
    SetLastError(87);
    return FALSE;
  }
  SIZE_T n = a->mapped;
  a->magic = 0;
  return gaia_unmap(a, n) == 0;
}
SIZE_T WINAPI RtlSizeHeap(HANDLE h, ULONG f, const void *p) {
  if (!p || h != (HANDLE)1 || f)
    return (SIZE_T)-1;
  const struct allocation *a = (const struct allocation *)p - 1;
  return a->magic == ALLOC_MAGIC ? a->size : (SIZE_T)-1;
}
void *WINAPI RtlReAllocateHeap(HANDLE h, ULONG f, void *p, SIZE_T n) {
  if (!p)
    return RtlAllocateHeap(h, f, n);
  SIZE_T old = RtlSizeHeap(h, 0, p);
  if (old == (SIZE_T)-1)
    return 0;
  void *q = RtlAllocateHeap(h, f, n);
  if (!q)
    return 0;
  memcpy(q, p, n < old ? n : old);
  RtlFreeHeap(h, 0, p);
  return q;
}
static uint32_t initialized;
static unsigned exiting;
void WINAPI RtlExitUserProcess(UINT code) {
  if (!__atomic_exchange_n(&exiting, 1, __ATOMIC_ACQ_REL)) {
    struct gaia_win32_boot *b = NtCurrentTeb()->ProcessEnvironmentBlock->GaiaBoot;
    while (initialized) {
      uint32_t i                                   = b->init_order[--initialized];
      BOOL(WINAPI * entry)(HMODULE, DWORD, void *) = (void *)(uintptr_t)b->modules[i].entry;
      if (entry)
        entry((void *)(uintptr_t)b->modules[i].base, DLL_PROCESS_DETACH, (void *)1);
    }
  }
  gaia_call(SYS_EXIT_GROUP, code, 0, 0, 0, 0, 0);
  for (;;) {
  }
}
static uint32_t tls_used[2];
DWORD WINAPI TlsAlloc(void) {
  for (DWORD i = 0; i < 64; i++) {
    uint32_t bit = 1u << (i % 32);
    if (!(__atomic_fetch_or(&tls_used[i / 32], bit, __ATOMIC_ACQ_REL) & bit)) {
      NtCurrentTeb()->TlsSlots[i] = 0;
      return i;
    }
  }
  SetLastError(8);
  return TLS_OUT_OF_INDEXES;
}
BOOL WINAPI TlsFree(DWORD i) {
  if (i >= 64 || !(__atomic_fetch_and(&tls_used[i / 32], ~(1u << (i % 32)), __ATOMIC_ACQ_REL) &
                   (1u << (i % 32)))) {
    SetLastError(87);
    return FALSE;
  }
  NtCurrentTeb()->TlsSlots[i] = 0;
  return TRUE;
}
void *WINAPI TlsGetValue(DWORD i) {
  if (i >= 64) {
    SetLastError(87);
    return 0;
  }
  SetLastError(0);
  return NtCurrentTeb()->TlsSlots[i];
}
BOOL WINAPI TlsSetValue(DWORD i, void *v) {
  if (i >= 64 || !(__atomic_load_n(&tls_used[i / 32], __ATOMIC_ACQUIRE) & (1u << (i % 32)))) {
    SetLastError(87);
    return FALSE;
  }
  NtCurrentTeb()->TlsSlots[i] = v;
  return TRUE;
}
BOOL GaiaInitializeDlls(void) {
  struct gaia_win32_boot *b = NtCurrentTeb()->ProcessEnvironmentBlock->GaiaBoot;
  while (initialized < b->init_count) {
    uint32_t index = b->init_order[initialized++];
    if (index >= b->module_count)
      return FALSE;
    BOOL(WINAPI * entry)(HMODULE, DWORD, void *) = (void *)(uintptr_t)b->modules[index].entry;
    if (entry && !entry((void *)(uintptr_t)b->modules[index].base, DLL_PROCESS_ATTACH, 0))
      return FALSE;
  }
  return TRUE;
}
void GaiaInitializeProcess(void) {
  TEB *t       = NtCurrentTeb();
  t->ProcessId = gaia_pid();
  t->ThreadId  = gaia_tid();
  if (!GaiaInitializeDlls())
    RtlExitUserProcess(1114);
  struct gaia_win32_boot *b = t->ProcessEnvironmentBlock->GaiaBoot;
  int(WINAPI * entry)(void) = (void *)(uintptr_t)b->image_entry;
  RtlExitUserProcess((UINT)entry());
}
NTSTATUS WINAPI NtClose(HANDLE h) { return GaiaHandleClose(h) ? 0 : (NTSTATUS)0xc0000008u; }
