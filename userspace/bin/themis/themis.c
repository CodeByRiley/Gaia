#define THEMIS_DECLARE_STATE
#include "themis.h"
#include "themis_prototypes.h"

#include <lib/app_info.h>
#include <lib/syscall.h>
#include <stdio.h>
#include <string.h>

APP_INFO(APP_TYPE_GUI, "Themis System Settings");

int main(int argc, char **argv) {
  themis_current_page = themis_find_page("system");

  if (argc > 1) {
    const struct themis_page *page = themis_resolve_page(argv[1]);

    if (page)
      themis_current_page = page;
  }

  if (themis_window_create() != 0)
    return 1;

  memset(&themis_ui, 0, sizeof(themis_ui));

  while (themis_running) {
    int saw_event = 0;
    themis_button_edges = 0;

    struct wm_event event;

    while (themis_button_edges < 1 && wm_poll_event(&event)) {
      saw_event = 1;
      themis_handle_event(&event);
    }

    if (!themis_running)
      break;

    if (!themis_redraw && !saw_event) {
      sleep_ticks(1);
      continue;
    }

    themis_render();
    yield();
  }

  wm_window_destroy(themis_window.handle);
  printf("themis: exit\n");

  return 0;
}
