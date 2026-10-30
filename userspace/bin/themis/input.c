#define THEMIS_DECLARE_STATE
#include "themis.h"
#include "themis_prototypes.h"

void themis_handle_mouse(int x, int y, int buttons) {
  themis_mouse_x = x;
  themis_mouse_y = y;
  themis_mouse_buttons = buttons;
  themis_redraw = 1;
}

void themis_handle_keyboard(int keycode) {
  // Keyboard navigation comes later.
  (void)keycode;
}

void themis_handle_event(const struct wm_event *event) {
  if (!event)
    return;

  switch (event->type) {
  case WM_EV_QUIT:
    themis_running = 0;
    break;

  case WM_EV_RESIZE:
    themis_window_resize(event);
    break;

  case WM_EV_MOUSE_MOVE:
    themis_handle_mouse(event->x, event->y, themis_mouse_buttons);
    break;

  case WM_EV_MOUSE_DOWN:
    themis_handle_mouse(event->x, event->y,
                        themis_mouse_buttons | event->param);
    themis_button_edges++;
    break;

  case WM_EV_MOUSE_UP:
    themis_handle_mouse(event->x, event->y,
                        themis_mouse_buttons & ~event->param);
    themis_button_edges++;
    break;

  case WM_EV_KEY_DOWN:
    themis_handle_keyboard(event->param);
    break;

  default:
    break;
  }
}
