/* userspace/lib/ui.c , immediate-mode widgets over lib/gfx.
 *
 * The interaction model is the usual two-id one: `hot` is whatever the
 * pointer is over, `active` is whatever took the press. A click is a
 * release while both are the same widget, which is what makes dragging
 * off a button cancel it instead of firing.
 *
 * Ids come from call order (c->next_id), so a frame that calls widgets in
 * a different sequence renumbers them and can hand a press to the wrong
 * one. That is the standard immediate-mode trade: no registration, no
 * teardown, at the price of a stable call order.
 */
#include "ui.h"
#include <lib/syscall.h>   /* MOUSE_BTN_LEFT */

const struct ui_theme ui_theme_default = {
    .face        = 0x00C0C0C0u,
    .face_hover  = 0x00D8D8D8u,
    .face_active = 0x00A0A0A0u,
    .text        = 0x00000000u,
    .text_muted  = 0x00808080u,
    .light       = 0x00FFFFFFu,
    .dark        = 0x00808080u,
    .accent      = 0x000000A0u,
    .accent_text = 0x00FFFFFFu,
    .pad         = 4,
    .border      = 1,
    .scale       = 1,
};

/* Frame */

void ui_begin(struct ui_context *c, struct gfx_surface *s,
              const struct ui_theme *theme,
              int mouse_x, int mouse_y, int buttons) {
    c->s        = s;
    c->theme    = theme ? theme : &ui_theme_default;
    c->mx       = mouse_x;
    c->my       = mouse_y;
    c->was_buttons = c->buttons;
    c->buttons     = buttons;
    c->was_down    = c->down;
    c->down        = (buttons & MOUSE_BTN_LEFT) ? 1 : 0;
    c->hot      = 0;
    c->next_id  = 0;
}

void ui_end(struct ui_context *c) {
    /* Whatever happened this frame, the press ends when the button does.
     * Clearing here rather than in the widget means a release over empty
     * space still cancels a pending press. */
    if (!c->down) c->active = 0;
}

static int menu_item_width(struct ui_context *c,
                           const struct ui_menu_item *items, int count) {
    int width = 72;
    const struct ui_theme *t = c->theme;
    for (int i = 0; items && i < count; i++) {
        int item_width = 0;
        int item_height = 0;
        gfx_text_size(items[i].label ? items[i].label : "", t->scale,
                      &item_width, &item_height);
        if (item_width + t->pad * 2 + 12 > width)
            width = item_width + t->pad * 2 + 12;
    }
    if (width > c->s->w - 8)
        width = c->s->w - 8;
    if (width < 32)
        width = 32;
    return width;
}

static int menu_height(int count) {
    if (count < 1)
        count = 1;
    return count * 18 + 4;
}

static struct gfx_rect menu_rect(struct ui_context *c, int x, int y,
                                 const struct ui_menu_item *items,
                                 int count) {
    int width = menu_item_width(c, items, count);
    int height = menu_height(count);
    if (x + width > c->s->w - 2)
        x = c->s->w - width - 2;
    if (x < 2)
        x = 2;
    if (y + height > c->s->h - 2)
        y = c->s->h - height - 2;
    if (y < 2)
        y = 2;
    return gfx_rect_make(x, y, width, height);
}

static int draw_menu_popup(struct ui_context *c,
                           const struct ui_menu_item *items, int count,
                           int x, int y, int id_base) {
    if (!items || count <= 0)
        return 0;
    const struct ui_theme *t = c->theme;
    struct gfx_rect popup = menu_rect(c, x, y, items, count);
    gfx_fill(c->s, popup, t->face);
    gfx_bevel(c->s, popup, t->light, t->dark, t->border);

    int result = 0;
    /* The popup is painted after the caller's content widgets.  Claim a new
     * primary press here so a row underneath the popup cannot also activate. */
    if (c->down && !c->was_down)
        c->active = 0;
    for (int i = 0; i < count; i++) {
        struct gfx_rect row = gfx_rect_make(popup.x + 2, popup.y + 2 + i * 18,
                                            popup.w - 4, 18);
        int row_id = id_base + i;
        if (c->down && !c->was_down && items[i].enabled &&
            gfx_rect_contains(row, c->mx, c->my))
            c->active = row_id;
        int clicked = ui_button_id(c, row_id, row, "");
        int hover = gfx_rect_contains(row, c->mx, c->my);
        uint32_t background = hover && items[i].enabled ? t->accent :
                              t->face;
        uint32_t foreground = hover && items[i].enabled ? t->accent_text :
                              (items[i].enabled ? t->text : t->text_muted);
        gfx_fill(c->s, row, background);
        gfx_text_box(c->s, row, items[i].label ? items[i].label : "",
                     foreground, t->scale, t->pad, GFX_TEXT_LEFT);
        if (clicked && items[i].enabled)
            result = items[i].id;
    }
    return result;
}

int ui_menu_bar_item(struct ui_context *c, struct ui_menu_state *state,
                     int menu_id, struct gfx_rect title,
                     const char *label,
                     const struct ui_menu_item *items, int item_count) {
    if (!c || !state)
        return 0;

    /* A release outside the currently open popup dismisses it.  This check
     * happens before the current title is drawn so clicking another title
     * still opens that title's menu in the same frame. */
    int title_press_active = c->active >= 0x4000 && c->active < 0x4100;
    if (state->open && !title_press_active && !c->down && c->was_down) {
        struct gfx_rect popup = gfx_rect_make(state->x, state->y,
                                              state->w, state->h);
        if (!gfx_rect_contains(popup, c->mx, c->my) &&
            !gfx_rect_contains(title, c->mx, c->my))
            state->open = 0;
    }

    int clicked_title = ui_button_id(c, 0x4000 + menu_id, title, label);
    if (clicked_title) {
        state->open = 1;
        state->menu_id = menu_id;
        state->x = title.x;
        state->y = title.y + title.h;
        struct gfx_rect popup = menu_rect(c, state->x, state->y,
                                          items, item_count);
        state->x = popup.x;
        state->y = popup.y;
        state->w = popup.w;
        state->h = popup.h;
    }

    if (!state->open || state->menu_id != menu_id)
        return 0;
    int result = draw_menu_popup(c, items, item_count, state->x, state->y,
                                 0x4100 + menu_id * 32);
    if (result)
        state->open = 0;
    return result;
}

int ui_context_menu(struct ui_context *c, struct ui_menu_state *state,
                    struct gfx_rect anchor,
                    const struct ui_menu_item *items, int item_count) {
    if (!c || !state || !items || item_count <= 0)
        return 0;

    int right_pressed = (c->buttons & MOUSE_BTN_RIGHT) &&
                        !(c->was_buttons & MOUSE_BTN_RIGHT);
    if (right_pressed && gfx_rect_contains(anchor, c->mx, c->my)) {
        state->open = 1;
        state->x = c->mx;
        state->y = c->my;
        struct gfx_rect popup = menu_rect(c, state->x, state->y,
                                          items, item_count);
        state->x = popup.x;
        state->y = popup.y;
        state->w = popup.w;
        state->h = popup.h;
    } else if (state->open && right_pressed &&
               !gfx_rect_contains(anchor, c->mx, c->my)) {
        state->open = 0;
    }

    if (!state->open)
        return 0;
    struct gfx_rect popup = menu_rect(c, state->x, state->y, items, item_count);
    int result = draw_menu_popup(c, items, item_count, state->x, state->y,
                                 0x5000);
    if (result) {
        state->open = 0;
        return result;
    }
    if (!c->down && c->was_down &&
        !gfx_rect_contains(popup, c->mx, c->my))
        state->open = 0;
    return 0;
}

static int ui_next_id(struct ui_context *c) {
    return ++c->next_id;
}

/* Shared hit/press bookkeeping. Returns 1 on a completed click. */
static int ui_interact(struct ui_context *c, int id, struct gfx_rect r,
                       int *out_hover, int *out_held) {
    if (id <= 0) {
        if (out_hover) *out_hover = 0;
        if (out_held)  *out_held  = 0;
        return 0;
    }

    int hover = gfx_rect_contains(r, c->mx, c->my) && !gfx_rect_empty(r);
    if (hover) c->hot = id;

    /* A press starts only on the transition, so holding the button while
     * the pointer wanders onto a widget does not arm it. */
    if (hover && c->down && !c->was_down) c->active = id;

    int held = (c->active == id) && c->down;
    int clicked = 0;
    if ((c->active == id) && !c->down && c->was_down && hover) clicked = 1;

    if (out_hover) *out_hover = hover;
    if (out_held)  *out_held  = held;
    return clicked;
}

/* Layout */

void ui_layout_begin(struct ui_layout *l, struct gfx_rect area, int gap) {
    l->area = area;
    l->y    = area.y;
    l->gap  = gap;
}

struct gfx_rect ui_layout_row(struct ui_layout *l, int h) {
    if (l->y >= l->area.y + l->area.h)
        return gfx_rect_make(l->area.x, l->y, 0, 0);

    int avail = (l->area.y + l->area.h) - l->y;
    if (h > avail) h = avail;

    struct gfx_rect r = gfx_rect_make(l->area.x, l->y, l->area.w, h);
    l->y += h + l->gap;
    return r;
}

struct gfx_rect ui_layout_column(struct gfx_rect row, int n, int i, int gap) {
    if (n <= 0 || i < 0 || i >= n) return gfx_rect_make(row.x, row.y, 0, 0);

    int total_gap = gap * (n - 1);
    int w = (row.w - total_gap) / n;
    if (w < 0) w = 0;
    return gfx_rect_make(row.x + i * (w + gap), row.y, w, row.h);
}

/* Chrome */

void ui_panel(struct ui_context *c, struct gfx_rect r) {
    const struct ui_theme *t = c->theme;
    gfx_fill(c->s, r, t->face);
    gfx_bevel(c->s, r, t->light, t->dark, t->border);
}

void ui_well(struct ui_context *c, struct gfx_rect r) {
    const struct ui_theme *t = c->theme;
    gfx_fill(c->s, r, t->face_active);
    /* Light and dark swapped: the same bevel read as sunken. */
    gfx_bevel(c->s, r, t->dark, t->light, t->border);
}

void ui_separator(struct ui_context *c, struct gfx_rect r) {
    const struct ui_theme *t = c->theme;
    int y = r.y + r.h / 2;
    gfx_hline(c->s, r.x, y,     r.w, t->dark);
    gfx_hline(c->s, r.x, y + 1, r.w, t->light);
}

/* Text */

/* Vertically centre one line of text in `r`, clipped to its width. */
static void text_in(struct ui_context *c, struct gfx_rect r,
                    const char *text, uint32_t color, int centered) {
    if (!text || gfx_rect_empty(r)) return;
    const struct ui_theme *t = c->theme;
    gfx_text_box(c->s, r, text, color, t->scale, t->pad,
                 centered ? GFX_TEXT_CENTER : GFX_TEXT_LEFT);
}

void ui_label(struct ui_context *c, struct gfx_rect r, const char *text) {
    text_in(c, r, text, c->theme->text, 0);
}

void ui_label_centered(struct ui_context *c, struct gfx_rect r,
                       const char *text) {
    text_in(c, r, text, c->theme->text, 1);
}

void ui_label_muted(struct ui_context *c, struct gfx_rect r,
                    const char *text) {
    text_in(c, r, text, c->theme->text_muted, 0);
}

/* Widgets */

int ui_button(struct ui_context *c, struct gfx_rect r, const char *label) {
    return ui_button_id(c, ui_next_id(c), r, label);
}

static int button_box(struct ui_context *c, int id, struct gfx_rect r,
                      int *out_held) {
    if (gfx_rect_empty(r)) return 0;

    int hover, held;
    int clicked = ui_interact(c, id, r, &hover, &held);

    const struct ui_theme *t = c->theme;
    uint32_t face = held ? t->face_active : (hover ? t->face_hover : t->face);

    gfx_fill(c->s, r, face);
    /* Pressed buttons invert the bevel, and the label shifts a pixel down
     * and right , the cheapest way to read as physically depressed. */
    if (held) gfx_bevel(c->s, r, t->dark, t->light, t->border);
    else      gfx_bevel(c->s, r, t->light, t->dark, t->border);

    if (out_held) *out_held = held;
    return clicked;
}

static int note_button_click(struct ui_context *c, int id,
                             struct gfx_rect r) {
    uint32_t now = (uint32_t)get_ticks();
    int dx = c->mx - c->last_click_x;
    int dy = c->my - c->last_click_y;
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    int doubled = c->last_click_id == id &&
                  now - c->last_click_tick < UI_DOUBLE_CLICK_TICKS &&
                  dx <= UI_DOUBLE_CLICK_SLOP && dy <= UI_DOUBLE_CLICK_SLOP;
    c->last_click_id = id;
    c->last_click_x = c->mx;
    c->last_click_y = c->my;
    c->last_click_tick = now;
    (void)r;
    return doubled;
}

int ui_button_id_double(struct ui_context *c, int id, struct gfx_rect r,
                        const char *label, int *double_clicked) {
    const struct ui_theme *t = c->theme;
    int held = 0;
    int clicked = button_box(c, id, r, &held);

    struct gfx_rect lr = r;
    if (held) { lr.x += 1; lr.y += 1; }
    text_in(c, lr, label, t->text, 1);

    if (double_clicked)
        *double_clicked = clicked ? note_button_click(c, id, r) : 0;
    return clicked;
}

int ui_button_id(struct ui_context *c, int id, struct gfx_rect r,
                 const char *label) {
    return ui_button_id_double(c, id, r, label, 0);
}

int ui_icon_button(struct ui_context *c, struct gfx_rect r,
                   const uint8_t *mask, int mw, int mh,
                   uint32_t icon_color) {
    return ui_icon_button_id(c, ui_next_id(c), r, mask, mw, mh, icon_color);
}

int ui_icon_button_id(struct ui_context *c, int id, struct gfx_rect r,
                      const uint8_t *mask, int mw, int mh,
                      uint32_t icon_color) {
    int held = 0;
    int clicked = button_box(c, id, r, &held);

    if (mask && mw > 0 && mh > 0 && !gfx_rect_empty(r)) {
        const struct ui_theme *t = c->theme;
        struct gfx_rect inner = gfx_rect_inset(r, t->border + 1);
        int scale = t->scale < 1 ? 1 : t->scale;
        while (scale > 1 && (mw * scale > inner.w || mh * scale > inner.h))
            scale--;

        int ix = r.x + (r.w - mw * scale) / 2;
        int iy = r.y + (r.h - mh * scale) / 2;
        if (held) { ix += 1; iy += 1; }
        gfx_mask(c->s, ix, iy, mask, mw, mh, icon_color, scale);
    }

    return clicked;
}

int ui_checkbox(struct ui_context *c, struct gfx_rect r,
                const char *label, int *value) {
    return ui_checkbox_id(c, ui_next_id(c), r, label, value);
}

int ui_checkbox_id(struct ui_context *c, int id, struct gfx_rect r,
                   const char *label, int *value) {
    if (gfx_rect_empty(r)) return 0;

    int hover, held;
    int clicked = ui_interact(c, id, r, &hover, &held);

    const struct ui_theme *t = c->theme;

    /* Square box on the left, label to its right. */
    int box = r.h < GFX_GLYPH_H * t->scale + 4 ? r.h : GFX_GLYPH_H * t->scale + 4;
    if (box < 4) box = 4;
    struct gfx_rect br = gfx_rect_make(r.x, r.y + (r.h - box) / 2, box, box);

    gfx_fill(c->s, br, hover ? t->face_hover : t->light);
    gfx_bevel(c->s, br, t->dark, t->light, t->border);

    if (value && *value) {
        /* A filled square rather than a tick: at 8px a tick is mush. */
        gfx_fill(c->s, gfx_rect_inset(br, 3), t->accent);
    }

    struct gfx_rect lr = gfx_rect_make(br.x + box, r.y,
                                       r.w - box, r.h);
    text_in(c, lr, label, t->text, 0);

    if (clicked && value) *value = !*value;
    return clicked;
}

void ui_progress(struct ui_context *c, struct gfx_rect r, int percent,
                 const char *label) {
    if (gfx_rect_empty(r)) return;
    const struct ui_theme *t = c->theme;

    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    ui_well(c, r);

    struct gfx_rect inner = gfx_rect_inset(r, t->border);
    if (!gfx_rect_empty(inner)) {
        struct gfx_rect fill = inner;
        fill.w = inner.w * percent / 100;
        gfx_fill(c->s, fill, t->accent);

        if (label) {
            /* Drawn over both the filled and empty halves in one pass;
             * the accent and the well are dark enough that a single light
             * colour stays readable across the boundary. */
            text_in(c, inner, label, t->accent_text, 1);
        }
    }
}

int ui_slider(struct ui_context *c, struct gfx_rect r,
              int min, int max, int *value) {
    return ui_slider_id(c, ui_next_id(c), r, min, max, value);
}

int ui_slider_id(struct ui_context *c, int id, struct gfx_rect r,
                 int min, int max, int *value) {
    if (gfx_rect_empty(r)) return 0;

    const struct ui_theme *t = c->theme;
    ui_well(c, r);

    if (!value || min >= max) return 0;

    int changed = 0;
    if (*value < min) { *value = min; changed = 1; }
    if (*value > max) { *value = max; changed = 1; }

    int hover, held;
    int clicked = ui_interact(c, id, r, &hover, &held);

    struct gfx_rect inner = gfx_rect_inset(r, t->border + 1);
    if (gfx_rect_empty(inner)) return changed;
    int thumb_w = r.h / 2;
    if (thumb_w < 8) thumb_w = 8;
    if (thumb_w > 14) thumb_w = 14;
    if (thumb_w > inner.w) thumb_w = inner.w;

    int left = inner.x + thumb_w / 2;
    int right = inner.x + inner.w - 1 - (thumb_w - 1) / 2;
    int span = right - left;
    if (span < 1) return changed;

    if (held || clicked) {
        int x = c->mx;
        if (x < left) x = left;
        if (x > right) x = right;
        int next = min + (int)(((int64_t)(x - left) * (max - min)
                              + span / 2) / span);
        if (next != *value) {
            *value = next;
            changed = 1;
        }
    }

    int thumb_x = left + (int)(((int64_t)(*value - min) * span)
                             / (max - min));
    int track_y = r.y + r.h / 2;
    gfx_hline(c->s, left, track_y, span + 1, t->dark);
    gfx_hline(c->s, left, track_y + 1, thumb_x - left + 1, t->accent);

    struct gfx_rect thumb = gfx_rect_make(thumb_x - thumb_w / 2,
                                           inner.y, thumb_w, inner.h);
    uint32_t face = held ? t->face_active : (hover ? t->face_hover : t->face);
    gfx_fill(c->s, thumb, face);
    if (held) gfx_bevel(c->s, thumb, t->dark, t->light, t->border);
    else      gfx_bevel(c->s, thumb, t->light, t->dark, t->border);
    return changed;
}
