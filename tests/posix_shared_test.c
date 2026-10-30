/* Execute the same service adapter on Linux, without a Gaia kernel fixture. */
#include <assert.h>
#include <shims/posix/shared/filesystem.h>
#include <shims/posix/shared/memory.h>
#include <shims/posix/shared/process.h>
#include <shims/posix/shared/time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void) {
  char name[128];
  snprintf(name, sizeof(name), "/tmp/gaia-shim-%lld",
           (long long)shim_process_id());
  int64_t fd =
      shim_open(name, GAIA_READ | GAIA_WRITE | GAIA_CREATE | GAIA_EXCLUSIVE);
  assert(fd >= 0);
  assert(shim_write(fd, "shared", 6) == 6 && shim_close(fd) == 0);
  assert(shim_read(fd, name, 1) < 0);
  fd = shim_open(name, GAIA_READ);
  assert(fd >= 0);
  char buf[8] = {0};
  assert(shim_read(fd, buf, 6) == 6 && !strcmp(buf, "shared"));
  assert(shim_close(fd) == 0);
  unlink(name);
  int64_t address = shim_map(NULL, 4096, 3);
  assert(address > 0);
  volatile int *p = (void *)(uintptr_t)address;
  *p = 42;
  assert(*p == 42 && shim_unmap((void *)p, 4096) == 0);
  uint64_t a, b;
  assert(!shim_monotonic_time(&a) && !shim_sleep(1000000) &&
         !shim_monotonic_time(&b) && b >= a);
  puts("Shared POSIX file/memory/time ownership and error contracts: passed");
  return 0;
}
