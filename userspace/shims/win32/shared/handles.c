#include <lib/services.h>
#include <shims/win32/handles.h>
#include <shims/win32/windows.h>
BOOL WINAPI RtlFreeHeap(HANDLE,ULONG,void *);
void GaiaCloseThreadHandle(int64_t);
struct slot {
  uint32_t generation;
  unsigned type, flags;
  int64_t value;
};
static struct slot slots[256];
static unsigned lock;
static void acquire(void) {
  while (__atomic_exchange_n(&lock, 1, __ATOMIC_ACQUIRE))
    gaia_yield();
}
static void release(void) { __atomic_store_n(&lock, 0, __ATOMIC_RELEASE); }
HANDLE GaiaHandleCreate(unsigned type, int64_t value, unsigned flags) {
  acquire();
  for (unsigned i = 0; i < 256; i++)
    if (!slots[i].type) {
      struct slot *s = slots + i;
      s->generation = (s->generation + 1) & 0x7fffff;
      if (!s->generation)
        s->generation = 1;
      s->value = value;
      s->flags = flags;
      s->type = type;
      HANDLE h =
          (void *)(uintptr_t)(((uint64_t)s->generation << 16) | (i + 256));
      release();
      return h;
    }
  release();
  SetLastError(ERROR_NOT_ENOUGH_MEMORY);
  return INVALID_HANDLE_VALUE;
}
BOOL GaiaHandleLookup(HANDLE h, unsigned type, int64_t *value,
                      unsigned *flags) {
  uintptr_t n = (uintptr_t)h;
  unsigned i = (n & 65535) - 256;
  acquire();
  BOOL ok = i < 256 && slots[i].type && (!type || slots[i].type == type) &&
            slots[i].generation == (n >> 16);
  if (ok) {
    if (value)
      *value = slots[i].value;
    if (flags)
      *flags = slots[i].flags;
  }
  release();
  if (!ok)
    SetLastError(ERROR_INVALID_HANDLE);
  return ok;
}
BOOL GaiaHandleClose(HANDLE h) {
  uintptr_t n = (uintptr_t)h;
  unsigned i = (n & 65535) - 256;
  acquire();
  if (i >= 256 || !slots[i].type || slots[i].generation != (n >> 16)) {
    release();
    SetLastError(6);
    return FALSE;
  }
  unsigned type = slots[i].type;
  int64_t value = slots[i].value;
  slots[i].type = 0;
  release();
  if (type == GH_FILE) {
    int64_t r = gaia_close((int)value);
    if (r < 0) {
      SetLastError(GaiaError(r));
      return FALSE;
    }
  }
  if(type==GH_EVENT)RtlFreeHeap((HANDLE)1,0,(void *)(uintptr_t)value);
  if(type==GH_THREAD)GaiaCloseThreadHandle(value);
  return TRUE;
}

unsigned GaiaHandleType(HANDLE h){uintptr_t n=(uintptr_t)h;unsigned i=(n&65535)-256;acquire();unsigned type=i<256&&slots[i].generation==(n>>16)?slots[i].type:0;release();return type;}
