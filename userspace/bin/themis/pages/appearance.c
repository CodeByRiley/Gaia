#define THEMIS_DECLARE_STATE
#include "../themis.h"
#include "../themis_prototypes.h"

void themis_page_appearance(struct ui_context *ui, struct gfx_rect area) {
  ui_group_box(ui, gfx_rect_make(area.x, area.y + 40, area.w, 140),
               "Appearance Configuration");
}
