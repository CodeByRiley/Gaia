#ifndef THEMIS_PROTOTYPES_H
#define THEMIS_PROTOTYPES_H

#include <lib/gfx.h>
#include <lib/ui.h>
#include <lib/wm.h>
#include <stddef.h>

// Application
void themis_init(void);
void themis_shutdown(void);

// Window management
int themis_window_create(void);
void themis_window_resize(const struct wm_event *event);

// Rendering
void themis_render(void);
void themis_render_sidebar(void);
void themis_render_content(void);

// Input
void themis_handle_mouse(int x, int y, int buttons);
void themis_handle_keyboard(int keycode);
void themis_handle_event(const struct wm_event *event);

// Settings
void themis_settings_load(void);
void themis_settings_save(void);

// Pages
const struct themis_page *themis_pages(size_t *count);
const struct themis_page *themis_find_page(const char *id);
const struct themis_page *themis_resolve_page(const char *id);

void themis_page_system(struct ui_context *ui, struct gfx_rect area);
void themis_page_display(struct ui_context *ui, struct gfx_rect area);
void themis_page_appearance(struct ui_context *ui, struct gfx_rect area);
void themis_page_audio(struct ui_context *ui, struct gfx_rect area);
void themis_page_about(struct ui_context *ui, struct gfx_rect area);

#endif
