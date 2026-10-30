#ifndef GAIA_WIN32_ABI_H
#define GAIA_WIN32_ABI_H
#include <stdint.h>
#define GAIA_WIN32_MAX_MODULES 32
#define GAIA_WIN32_TEB 0x0000000600000000ULL
#define GAIA_WIN32_PEB (GAIA_WIN32_TEB + 0x2000)
#define GAIA_WIN32_BOOT (GAIA_WIN32_TEB + 0x3000)
struct gaia_win32_module {
  uint64_t base, entry;
  char name[64];
};
struct gaia_win32_boot {
  uint64_t image_entry;
  uint32_t module_count, init_count;
  uint32_t init_order[GAIA_WIN32_MAX_MODULES];
  struct gaia_win32_module modules[GAIA_WIN32_MAX_MODULES];
  char command_line[2048];
};
#endif
