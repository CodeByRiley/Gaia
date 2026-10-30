#define THEMIS_DECLARE_STATE
#include "themis.h"
#include "themis_prototypes.h"

struct themis_category {
  const char *name;
  int expanded;
};

static struct themis_category categories[] = {
    {"General",     1},
    {"Information", 1},
};

static const struct ui_theme themis_theme = {
    .face        = THEMIS_MAIN_BACKGROUND,
    .face_hover  = 0x00D8E8F0u,
    .face_active = 0x00C0D8E8u,
    .text        = THEMIS_TEXT,
    .text_muted  = 0x00808080u,
    .light       = 0x00FFFFFFu,
    .dark        = 0x00808080u,
    .accent      = THEMIS_ACCENTS,
    .accent_text = 0x00FFFFFFu,
    .pad         = 4,
    .border      = 1,
    .scale       = 1,
};

static void themis_render_footer(void) {
  int y        = themis_surface.h - THEMIS_FOOTER_H;
  int button_w = 112;
  int button_h = 26;
  int gap      = 12;

  gfx_hline(&themis_surface, 0, y, themis_surface.w, THEMIS_BORDERS);

  int x = themis_surface.w - (button_w * 3 + gap * 2) - 16;

  struct gfx_rect defaults = gfx_rect_make(x, y + 12, button_w, button_h);

  struct gfx_rect apply = gfx_rect_make(x + button_w + gap, y + 12, button_w, button_h);

  struct gfx_rect close = gfx_rect_make(x + (button_w + gap) * 2, y + 12, button_w, button_h);

  // Disabled until settings persistence exists.
  ui_panel(&themis_ui, defaults);
  ui_label_muted(&themis_ui, defaults, "Reset to default.");

  ui_panel(&themis_ui, apply);
  ui_label_muted(&themis_ui, apply, "Apply");

  if (ui_button_id(&themis_ui, 200, close, "Close"))
    themis_running = 0;
}

void themis_render_content(void) {
  int x = THEMIS_LAYOUT_PAD * 2 + THEMIS_SIDEBAR_W;
  int w = themis_surface.w - x - THEMIS_LAYOUT_PAD;
  int h = themis_surface.h - THEMIS_FOOTER_H - THEMIS_LAYOUT_PAD * 2;

  if (w <= 0 || h <= 0)
    return;

  struct gfx_rect content = gfx_rect_make(x, THEMIS_LAYOUT_PAD, w, h);

  gfx_fill(&themis_surface, content, THEMIS_MAIN_BACKGROUND);

  if (themis_current_page && themis_current_page->render)
    themis_current_page->render(&themis_ui, content);
}

void themis_render(void) {
  themis_redraw = 0;

  ui_begin(&themis_ui,
           &themis_surface,
           &themis_theme,
           themis_mouse_x,
           themis_mouse_y,
           themis_mouse_buttons);

  gfx_clear(&themis_surface, THEMIS_MAIN_BACKGROUND);

  themis_render_sidebar();
  themis_render_content();
  themis_render_footer();

  ui_end(&themis_ui);

  wm_window_invalidate(themis_window.handle);
}

static void themis_nav_item(int id, const struct themis_page *page, int y) {
  struct gfx_rect row = gfx_rect_make(32, y, THEMIS_SIDEBAR_W - 48, 22);

  int clicked = ui_button_id(&themis_ui, id, row, "");

  int selected = themis_current_page == page;
  int hover    = gfx_rect_contains(row, themis_ui.mx, themis_ui.my);

  uint32_t bg = selected ? THEMIS_ACCENTS : hover ? 0x00E0E8E8u : THEMIS_NAV_BACKGROUND;

  uint32_t fg = selected ? 0x00FFFFFFu : THEMIS_TEXT;

  gfx_fill(&themis_surface, row, bg);

  gfx_text_box(&themis_surface, row, page->name, fg, 1, 6, GFX_TEXT_LEFT);

  if (clicked) {
    themis_current_page = page;
    themis_redraw       = 1;
  }
}
void themis_render_sidebar(void) {
  struct gfx_rect nav = gfx_rect_make(THEMIS_LAYOUT_PAD,
                                      THEMIS_LAYOUT_PAD,
                                      THEMIS_SIDEBAR_W,
                                      themis_surface.h - THEMIS_FOOTER_H - THEMIS_LAYOUT_PAD * 2);

  gfx_fill(&themis_surface, nav, THEMIS_NAV_BACKGROUND);
  gfx_bevel(&themis_surface, nav, 0x00808080u, 0x00FFFFFFu, 2);

  size_t count;
  const struct themis_page *pages = themis_pages(&count);

  int y = nav.y + 10;

  struct gfx_rect old_clip = gfx_clip_push(&themis_surface, nav);

  for (size_t c = 0; c < sizeof(categories) / sizeof(categories[0]); c++) {
    struct themis_category *category = &categories[c];

    struct gfx_rect heading = gfx_rect_make(nav.x + 10, y, nav.w - 20, 22);

    if (ui_button_id(&themis_ui, 100 + (int)c, heading, "")) {
      category->expanded = !category->expanded;
      themis_redraw      = 1;
    }

    // Cover the standard button to draw a tree heading.
    gfx_fill(&themis_surface, heading, THEMIS_NAV_BACKGROUND);

    gfx_text_box(&themis_surface,
                 gfx_rect_make(heading.x + 2, heading.y, 26, heading.h),
                 category->expanded ? "[-]" : "[+]",
                 THEMIS_TEXT,
                 1,
                 0,
                 GFX_TEXT_LEFT);

    gfx_text_box(&themis_surface,
                 gfx_rect_make(heading.x + 30, heading.y, heading.w - 30, heading.h),
                 category->name,
                 THEMIS_TEXT,
                 1,
                 0,
                 GFX_TEXT_LEFT);

    y += 26;

    if (!category->expanded)
      continue;

    for (size_t i = 0; i < count; i++) {
      if (strcmp(pages[i].category, category->name) != 0)
        continue;

      themis_nav_item(1000 + (int)i, &pages[i], y);
      y += 24;
    }
  }

  gfx_clip_set(&themis_surface, old_clip);
}
