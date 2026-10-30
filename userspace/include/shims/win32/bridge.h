#ifndef GAIA_WIN32_BRIDGE_H
#define GAIA_WIN32_BRIDGE_H
#include "windows.h"
#include <lib/wm.h>
struct gaia_dc {
  struct wm_window window;
  DWORD text_color, background_color;
  INT background_mode;
};
struct gaia_dc *GaiaGetDC(HDC);
void GaiaPresentDC(HDC);
#endif
