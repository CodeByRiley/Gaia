#include "hermes_prototypes.h"
#include <lib/syscall.h>
#include <stdio.h>
#include <string.h>

static const struct ui_menu_item file_menu_items[] = {
    {"Open", MENU_OPEN, 1},
    {"New Folder", MENU_NEW, 1},
    {"Delete", MENU_DELETE, 1},
    {"Exit", MENU_EXIT, 1},
};
static const struct ui_menu_item edit_menu_items[] = {
    {"Up", MENU_UP, 1},
    {"Refresh", MENU_REFRESH, 1},
};
static const struct ui_menu_item view_menu_items[] = {
    {"Details", MENU_DETAILS, 1},
};
static const struct ui_menu_item tools_menu_items[] = {
    {"Folder Options", MENU_OPTIONS, 1},
};
static const struct ui_menu_item help_menu_items[] = {
    {"About Hermes", MENU_ABOUT, 1},
};
static const struct ui_menu_item context_menu_items[] = {
    {"Open", CONTEXT_OPEN, 1},
    {"New Folder", CONTEXT_NEW, 1},
    {"Delete", CONTEXT_DELETE, 1},
    {"Go Up", CONTEXT_UP, 1},
};

/* Hermes deliberately draws its own small Explorer icons.  Keeping them in
 * the render module leaves the filesystem/event module focused on behavior. */
static void draw_folder_icon(struct gfx_surface *surface, int x, int y,
                             int open) {
    uint32_t tab = open ? 0x00FFFF80u : 0x00E0B040u;
    uint32_t body = open ? 0x00FFE070u : 0x00FFC040u;
    gfx_fill(surface, gfx_rect_make(x + 2, y + 1, 8, 4), tab);
    gfx_fill(surface, gfx_rect_make(x, y + 4, 15, 10), body);
    gfx_hline(surface, x, y + 4, 15, 0x00FFFFFFu);
    gfx_vline(surface, x, y + 4, 10, 0x00FFFFFFu);
    gfx_hline(surface, x, y + 13, 15, 0x00808000u);
    gfx_vline(surface, x + 14, y + 4, 10, 0x00808000u);
}

static void draw_file_icon(struct gfx_surface *surface, int x, int y) {
    gfx_fill(surface, gfx_rect_make(x + 2, y, 10, 14), WIN98_WHITE);
    gfx_frame(surface, gfx_rect_make(x + 2, y, 10, 14), WIN98_SHADOW, 1);
    gfx_hline(surface, x + 4, y + 5, 6, WIN98_BLUE);
    gfx_hline(surface, x + 4, y + 8, 6, WIN98_BLUE);
    gfx_hline(surface, x + 4, y + 11, 4, WIN98_BLUE);
    gfx_fill(surface, gfx_rect_make(x + 9, y + 1, 2, 2), 0x00D0D0D0u);
}

static void draw_plus_box(struct gfx_surface *surface, int x, int y,
                          int expanded) {
    struct gfx_rect box = gfx_rect_make(x, y, 9, 9);
    gfx_fill(surface, box, WIN98_WHITE);
    gfx_frame(surface, box, WIN98_TEXT, 1);
    gfx_hline(surface, x + 2, y + 4, 5, WIN98_TEXT);
    if (!expanded)
        gfx_vline(surface, x + 4, y + 2, 5, WIN98_TEXT);
}

static int draw_modal(struct ui_context *ui, struct gfx_surface *surface) {
    int width = surface->w - 20;
    if (width > 320)
        width = 320;
    if (width < 120)
        width = 120;
    int height = 104;
    int x = (surface->w - width) / 2;
    int y = (surface->h - height) / 2;
    struct gfx_rect modal = gfx_rect_make(x, y, width, height);
    ui_panel(ui, modal);

    const char *title = modal_action == MODAL_MKDIR ?
                            "New directory" :
                            (modal_target_is_dir ? "Delete directory" :
                                                   "Delete file");
    ui_label(ui, gfx_rect_make(x + 10, y + 6, width - 20, 18), title);

    struct gfx_rect input = gfx_rect_make(x + 10, y + 28, width - 20, 22);
    ui_well(ui, input);
    if (modal_action == MODAL_MKDIR) {
        char display[TEXT_INPUT_CAP + 1];
        size_t length = bounded_strlen(text_input, sizeof(text_input));
        memcpy(display, text_input, length);
        display[length++] = '_';
        display[length] = 0;
        ui_label(ui, gfx_rect_inset(input, 2), display);
    } else {
        ui_label(ui, gfx_rect_inset(input, 2), text_input);
    }

    struct gfx_rect buttons = gfx_rect_make(x + 10, y + 58, width - 20, 22);
    int changed = 0;
    if (ui_button_id(ui, UI_ID_CONFIRM,
                     ui_layout_column(buttons, 2, 0, 8), "Confirm")) {
        confirm_modal();
        changed = 1;
    }
    if (ui_button_id(ui, UI_ID_CANCEL,
                     ui_layout_column(buttons, 2, 1, 8), "Cancel")) {
        cancel_modal();
        changed = 1;
    }
    ui_label_muted(ui, gfx_rect_make(x + 10, y + 84, width - 20, 14),
                   "Enter confirms; Esc cancels");
    return changed;
}

static void draw_tree_row(struct ui_context *ui, struct gfx_surface *surface,
                          int id, struct gfx_rect row, int indent,
                          const char *label, int selected_row, int folder,
                          int expandable, int expanded) {
    ui_button_id(ui, id, row, "");
    gfx_fill(surface, row, selected_row ? WIN98_SELECT : WIN98_WHITE);
    int x = row.x + 4 + indent * 12;
    if (expandable)
        draw_plus_box(surface, x, row.y + 4, expanded);
    x += expandable ? 12 : 0;
    if (folder)
        draw_folder_icon(surface, x, row.y + 1, expanded);
    else
        draw_file_icon(surface, x, row.y + 1);
    gfx_text_box(surface, gfx_rect_make(x + 18, row.y,
                                        row.w - (x - row.x) - 20, row.h),
                 label, selected_row ? WIN98_WHITE : WIN98_TEXT, 1, 0,
                 GFX_TEXT_LEFT);
}

static int draw_tree(struct ui_context *ui, struct gfx_surface *surface,
                     struct gfx_rect pane) {
    int changed = 0;
    ui_well(ui, pane);
    struct gfx_rect inside = gfx_rect_inset(pane, 2);
    gfx_fill(surface, inside, WIN98_WHITE);
    int y = inside.y + 3;
    const int row_h = 18;

    struct gfx_rect desktop = gfx_rect_make(inside.x + 2, y, inside.w - 4,
                                            row_h);
    draw_tree_row(ui, surface, UI_ID_TREE_BASE, desktop, 0, "Desktop", 0, 0,
                  1, 1);
    y += row_h;
    struct gfx_rect computer = gfx_rect_make(inside.x + 2, y, inside.w - 4,
                                             row_h);
    draw_tree_row(ui, surface, UI_ID_TREE_BASE + 1, computer, 1, "My Computer",
                  0, 1, 1, 1);
    y += row_h;

    int depth = path_depth();
    struct gfx_rect root = gfx_rect_make(inside.x + 2, y, inside.w - 4, row_h);
    draw_tree_row(ui, surface, UI_ID_TREE_BASE + 2, root, 2, "Gaia (C:)",
                  depth == 0, 1, 1, depth > 0);
    if (ui->active == UI_ID_TREE_BASE + 2 && !ui->down && ui->was_down &&
        gfx_rect_contains(root, ui->mx, ui->my)) {
        go_to_tree_depth(0);
        changed = 1;
    }
    y += row_h;

    char component[MAX_NAME];
    for (int i = 0; i < depth && y + row_h <= inside.y + inside.h; i++) {
        struct gfx_rect row = gfx_rect_make(inside.x + 2, y, inside.w - 4,
                                            row_h);
        path_component(i, component, sizeof(component));
        int id = UI_ID_TREE_BASE + 3 + i;
        draw_tree_row(ui, surface, id, row, 3 + i, component, i + 1 == depth,
                      1, i + 1 < depth, i + 1 < depth);
        if (ui->active == id && !ui->down && ui->was_down &&
            gfx_rect_contains(row, ui->mx, ui->my)) {
            go_to_tree_depth(i + 1);
            changed = 1;
        }
        y += row_h;
    }
    return changed;
}

static int draw_details(struct ui_context *ui, struct gfx_surface *surface,
                        struct gfx_rect pane) {
    int changed = 0;
    ui_well(ui, pane);
    struct gfx_rect inside = gfx_rect_inset(pane, 2);
    gfx_fill(surface, inside, WIN98_WHITE);
    const int header_h = 19;
    struct gfx_rect header = gfx_rect_make(inside.x, inside.y, inside.w,
                                           header_h);
    gfx_fill(surface, header, WIN98_GREY);
    gfx_bevel(surface, header, WIN98_WHITE, WIN98_SHADOW, 1);

    int name_w = inside.w * 48 / 100;
    int type_w = inside.w * 27 / 100;
    struct gfx_rect name_header = gfx_rect_make(header.x + 2, header.y + 1,
                                                name_w - 3, header.h - 2);
    struct gfx_rect type_header = gfx_rect_make(name_header.x + name_header.w,
                                                header.y + 1, type_w,
                                                header.h - 2);
    struct gfx_rect size_header = gfx_rect_make(type_header.x + type_header.w,
                                                header.y + 1,
                                                inside.w - name_w - type_w - 3,
                                                header.h - 2);
    gfx_text_box(surface, name_header, "Name", WIN98_TEXT, 1, 3, GFX_TEXT_LEFT);
    gfx_text_box(surface, type_header, "Type", WIN98_TEXT, 1, 3, GFX_TEXT_LEFT);
    gfx_text_box(surface, size_header, "Size", WIN98_TEXT, 1, 3, GFX_TEXT_LEFT);
    gfx_vline(surface, type_header.x, header.y, header.h, WIN98_SHADOW);
    gfx_vline(surface, size_header.x, header.y, header.h, WIN98_SHADOW);

    const int row_h = 20;
    int visible = (inside.h - header_h) / row_h;
    if (visible < 1)
        visible = 1;
    if (selected < scroll)
        scroll = selected;
    if (selected >= scroll + visible)
        scroll = selected - visible + 1;
    if (scroll < 0)
        scroll = 0;

    for (int row_index = 0;
         row_index < visible && scroll + row_index < entry_count; row_index++) {
        int index = scroll + row_index;
        struct gfx_rect row = gfx_rect_make(inside.x, inside.y + header_h +
                                            row_index * row_h, inside.w, row_h);
        int double_clicked = 0;
        int clicked = ui_button_id_double(
            ui, UI_ID_ENTRY_BASE + index, row, "", &double_clicked);
        gfx_fill(surface, row, index == selected ? WIN98_SELECT : WIN98_WHITE);
        if (entries[index].is_dir)
            draw_folder_icon(surface, row.x + 4, row.y + 2, index == selected);
        else
            draw_file_icon(surface, row.x + 4, row.y + 2);

        uint32_t text = index == selected ? WIN98_WHITE : WIN98_TEXT;
        gfx_text_box(surface, gfx_rect_make(row.x + 23, row.y, name_w - 23,
                                            row.h), entries[index].name, text,
                     1, 2, GFX_TEXT_LEFT);
        const char *type = entries[index].is_dir ? "File Folder" :
                           (is_program_name(entries[index].name) ?
                            "Application" : "File");
        gfx_text_box(surface, gfx_rect_make(type_header.x, row.y, type_w,
                                            row.h), type, text, 1, 3,
                     GFX_TEXT_LEFT);
        gfx_text_box(surface, gfx_rect_make(size_header.x, row.y, size_header.w,
                                            row.h), entries[index].is_dir ? "" :
                                            "--", text, 1, 3, GFX_TEXT_RIGHT);
        if (clicked) {
            selected = index;
            changed = 1;
        }
        if (double_clicked) {
            activate_selected();
            changed = 1;
        }
    }
    return changed;
}

int draw_frame(struct ui_context *ui, struct gfx_surface *surface,
               int *running) {
    int changed = 0;
    int modal_down = ui->down;
    int modal_was_down = ui->was_down;
    if (modal_action != MODAL_NONE) {
        ui->down = 0;
        ui->was_down = 0;
    }
    struct gfx_rect bounds = gfx_surface_bounds(surface);
    gfx_fill(surface, bounds, ui->theme->face);

    if (surface->w < MIN_LAYOUT_W || surface->h < MIN_LAYOUT_H) {
        ui_label_centered(ui, bounds, "Window too small");
        return 0;
    }

    struct gfx_rect menu = gfx_rect_make(0, 0, surface->w, 20);
    gfx_fill(surface, menu, WIN98_GREY);
    gfx_hline(surface, 0, menu.y + menu.h - 1, surface->w, WIN98_SHADOW);

    struct gfx_rect toolbar = gfx_rect_make(0, menu.h, surface->w, 34);
    ui_panel(ui, toolbar);
    if (ui_button_id(ui, UI_ID_OPEN, gfx_rect_make(6, 24, 48, 24), "Back")) {
        set_status("No previous folder");
        changed = 1;
    }
    if (ui_button_id(ui, UI_ID_UP, gfx_rect_make(58, 24, 40, 24), "Up")) {
        go_up();
        changed = 1;
    }
    if (ui_button_id(ui, UI_ID_MKDIR, gfx_rect_make(106, 24, 48, 24), "New")) {
        open_mkdir_modal();
        changed = 1;
    }
    if (ui_button_id(ui, UI_ID_DELETE, gfx_rect_make(158, 24, 54, 24),
                     "Delete")) {
        open_delete_modal();
        changed = 1;
    }
    if (ui_button_id(ui, UI_ID_QUIT, gfx_rect_make(220, 24, 44, 24), "Close")) {
        *running = 0;
        changed = 1;
    }

    struct gfx_rect address = gfx_rect_make(0, toolbar.y + toolbar.h,
                                            surface->w, 30);
    gfx_fill(surface, address, WIN98_GREY);
    gfx_hline(surface, 0, address.y, surface->w, WIN98_WHITE);
    gfx_hline(surface, 0, address.y + address.h - 1, surface->w, WIN98_SHADOW);
    gfx_text(surface, 8, address.y + 11, "Address", WIN98_TEXT, 1);
    struct gfx_rect address_well = gfx_rect_make(66, address.y + 5,
                                                 surface->w - 116, 20);
    ui_well(ui, address_well);
    draw_folder_icon(surface, address_well.x + 4, address_well.y + 2, 1);
    gfx_text_box(surface, gfx_rect_make(address_well.x + 23, address_well.y,
                                        address_well.w - 26, address_well.h),
                 cwd, WIN98_TEXT, 1, 1, GFX_TEXT_LEFT);
    ui_button_id(ui, 8, gfx_rect_make(surface->w - 45, address.y + 5, 38, 20),
                 "Go");

    const int status_h = 22;
    struct gfx_rect content = gfx_rect_make(5, address.y + address.h + 4,
                                            surface->w - 10,
                                            surface->h - address.y - address.h -
                                            status_h - 9);
    int tree_w = content.w * 34 / 100;
    if (tree_w < 145)
        tree_w = 145;
    if (tree_w > 210)
        tree_w = 210;
    struct gfx_rect tree = gfx_rect_make(content.x, content.y, tree_w,
                                         content.h);
    struct gfx_rect details = gfx_rect_make(tree.x + tree.w + 4, content.y,
                                            content.w - tree.w - 4, content.h);
    changed |= draw_tree(ui, surface, tree);
    changed |= draw_details(ui, surface, details);

    struct gfx_rect status = gfx_rect_make(0, surface->h - status_h,
                                           surface->w, status_h);
    gfx_fill(surface, status, WIN98_GREY);
    gfx_hline(surface, 0, status.y, surface->w, WIN98_WHITE);
    struct gfx_rect message = gfx_rect_make(4, status.y + 3,
                                            status.w * 65 / 100, status.h - 6);
    ui_well(ui, message);
    gfx_text_box(surface, gfx_rect_inset(message, 1), status_text, WIN98_TEXT,
                 1, 2, GFX_TEXT_LEFT);
    char objects[32];
    snprintf(objects, sizeof(objects), "%d object%s", entry_count > 0 ?
             entry_count - 1 : 0, entry_count == 2 ? "" : "s");
    struct gfx_rect count = gfx_rect_make(message.x + message.w + 3,
                                          status.y + 3,
                                          status.w - message.w - 7,
                                          status.h - 6);
    ui_well(ui, count);
    gfx_text_box(surface, gfx_rect_inset(count, 1), objects, WIN98_TEXT, 1, 2,
                 GFX_TEXT_LEFT);

    if ((ui->buttons & MOUSE_BTN_RIGHT) &&
        !(ui->was_buttons & MOUSE_BTN_RIGHT))
        action_menu_state.open = 0;
    int menu_action = 0;
    menu_action |= ui_menu_bar_item(ui, &action_menu_state, MENU_FILE,
                                    gfx_rect_make(4, 1, 34, 18), "File",
                                    file_menu_items,
                                    (int)(sizeof(file_menu_items) /
                                          sizeof(file_menu_items[0])));
    menu_action |= ui_menu_bar_item(ui, &action_menu_state, MENU_EDIT,
                                    gfx_rect_make(40, 1, 34, 18), "Edit",
                                    edit_menu_items,
                                    (int)(sizeof(edit_menu_items) /
                                          sizeof(edit_menu_items[0])));
    menu_action |= ui_menu_bar_item(ui, &action_menu_state, MENU_VIEW,
                                    gfx_rect_make(76, 1, 40, 18), "View",
                                    view_menu_items,
                                    (int)(sizeof(view_menu_items) /
                                          sizeof(view_menu_items[0])));
    menu_action |= ui_menu_bar_item(ui, &action_menu_state, MENU_TOOLS,
                                    gfx_rect_make(118, 1, 42, 18), "Tools",
                                    tools_menu_items,
                                    (int)(sizeof(tools_menu_items) /
                                          sizeof(tools_menu_items[0])));
    menu_action |= ui_menu_bar_item(ui, &action_menu_state, MENU_HELP,
                                    gfx_rect_make(162, 1, 40, 18), "Help",
                                    help_menu_items,
                                    (int)(sizeof(help_menu_items) /
                                          sizeof(help_menu_items[0])));
    if (menu_action == MENU_EXIT)
        *running = 0;
    else if (menu_action) {
        run_menu_action(menu_action);
        changed = 1;
    }

    int context_action = ui_context_menu(
        ui, &context_menu_state, content, context_menu_items,
        (int)(sizeof(context_menu_items) / sizeof(context_menu_items[0])));
    if (context_action) {
        run_menu_action(context_action);
        changed = 1;
    }

    if (modal_action != MODAL_NONE) {
        ui->down = modal_down;
        ui->was_down = modal_was_down;
        changed |= draw_modal(ui, surface);
    }
    return changed;
}
