#ifndef GAIA_PEB_H
#define GAIA_PEB_H
#include "windef.h"
#include <arch/win32_abi.h>
typedef struct {
  BYTE reserved0[0x10];
  void *ImageBaseAddress;
  BYTE reserved1[0x100 - 0x18];
  struct gaia_win32_boot *GaiaBoot;
} PEB;
_Static_assert(offsetof(PEB, ImageBaseAddress) == 0x10, "PEB image offset");
_Static_assert(offsetof(PEB, GaiaBoot) == 0x100, "private PEB extension");
#endif
