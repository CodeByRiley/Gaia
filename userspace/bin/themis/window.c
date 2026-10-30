#define THEMIS_DECLARE_STATE
#include "themis.h"
#include "themis_prototypes.h"

#include <stdint.h>
#include <string.h>

int themis_window_create(void) {
  if (wm_window_create(THEMIS_WINDOW_W, THEMIS_WINDOW_H, "Themis",
                       &themis_window) != 0) {
    printf("themis: could not create window\n");
    return -1;
  }

  gfx_surface_init(
      &themis_surface, (uint32_t *)(uintptr_t)themis_window.surface_va,
      themis_window.w, themis_window.h, (int)(themis_window.pitch / 4));

  return 0;
}

void themis_window_resize(const struct wm_event *event) {
  if (!event)
    return;

  themis_window.surface_va = event->surface_va;
  themis_window.pitch = event->pitch;
  themis_window.w = event->w;
  themis_window.h = event->h;

  gfx_surface_init(
      &themis_surface, (uint32_t *)(uintptr_t)themis_window.surface_va,
      themis_window.w, themis_window.h, (int)(themis_window.pitch / 4));

  // Reset UI interaction state after resizing.
  memset(&themis_ui, 0, sizeof(themis_ui));

  themis_redraw = 1;
}
