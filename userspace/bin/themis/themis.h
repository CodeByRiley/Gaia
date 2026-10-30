#ifndef THEMIS_INTERNAL_H
#define THEMIS_INTERNAL_H

#include "themis_prototypes.h"
#include <lib/app_info.h>
#include <lib/gfx.h>
#include <lib/wm.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum {
  THEMIS_PAGE_SYSTEM,
  THEMIS_PAGE_DISPLAY,
  THEMIS_PAGE_APPEARANCE,
  THEMIS_PAGE_AUDIO,
  THEMIS_PAGE_ABOUT
} ThemisPage;

struct themis_alias {
  const char *alias;
  const char *page_id;
};

struct themis_page {
  const char *id;
  const char *name;
  const char *category;
  void (*render)(struct ui_context *, struct gfx_rect);
};

#define THEMIS_WINDOW_W 800
#define THEMIS_WINDOW_H 540

#define THEMIS_SIDEBAR_W 220
#define THEMIS_LAYOUT_PAD 12
#define THEMIS_FOOTER_H 52

/* Themis Colours */
#define THEMIS_MAIN_BACKGROUND 0x00E8E8E8
#define THEMIS_NAV_BACKGROUND 0x00F2F0E8
#define THEMIS_BORDERS 0x00D0D0D0
#define THEMIS_ACCENTS 0x000080C0
#define THEMIS_TEXT 0x00000000
#define THEMIS_SELECTED_BG 0x000080C0u
#define THEMIS_SELECTED_TEXT 0x00FFFFFFu
#define THEMIS_PANEL_BG 0x00E8E8E8u
#define THEMIS_BUTTON_BG 0x00D4D0C8u
#define THEMIS_BORDER_LIGHT 0x00FFFFFFu
#define THEMIS_BORDER_DARK 0x00808080u
#define THEMIS_DISABLED_TEXT 0x00808080u

#ifdef THEMIS_DECLARE_STATE
#define THEMIS_STATE extern
#define THEMIS_STATE_INIT(value)
#else
#define THEMIS_STATE
#define THEMIS_STATE_INIT(value) = value
#endif

THEMIS_STATE struct ui_context themis_ui;
THEMIS_STATE struct wm_window themis_window;
THEMIS_STATE struct gfx_surface themis_surface;
THEMIS_STATE const struct themis_page *
    themis_current_page THEMIS_STATE_INIT(NULL);

THEMIS_STATE int themis_mouse_x;
THEMIS_STATE int themis_mouse_y;
THEMIS_STATE int themis_mouse_buttons;
THEMIS_STATE int themis_button_edges;
THEMIS_STATE int themis_running THEMIS_STATE_INIT(1);
THEMIS_STATE int themis_redraw THEMIS_STATE_INIT(1);
THEMIS_STATE int themis_nav_general_expanded THEMIS_STATE_INIT(1);
THEMIS_STATE int themis_settings_dirty THEMIS_STATE_INIT(0);

#endif
