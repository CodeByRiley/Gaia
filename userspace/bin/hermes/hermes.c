/* userspace/bin/hermes/hermes.c - Hermes File Explorer.
 *
 * Traverses FAT32/16 directory entries and manages file/folder operations.
 * Operates visually alongside the SH(ell).ELF prompt, listening for keyboard
 * events to navigate directory trees, launch executables via spawn(), and
 * organize the local volume.
 *
 */

#include <lib/gfx.h>
#include <lib/keymap.h>
#include <lib/syscall.h>
#include <lib/app_info.h>
#include <lib/ui.h>
#include <lib/wm.h>
#include <include/key_codes.h>
#include <stdio.h>
#include <string.h>

#define WINDOW_W 560
#define WINDOW_H 380
#define MIN_LAYOUT_W 300
#define MIN_LAYOUT_H 180

#define MAX_ENTRIES 128
#define MAX_NAME 256
#define MAX_PATH 256
#define STATUS_CAP 128
#define TEXT_INPUT_CAP MAX_NAME

#define MODAL_NONE 0
#define MODAL_MKDIR 1
#define MODAL_DELETE 2

#define UI_ID_UP 1
#define UI_ID_OPEN 2
#define UI_ID_MKDIR 3
#define UI_ID_DELETE 4
#define UI_ID_QUIT 5
#define UI_ID_CONFIRM 6
#define UI_ID_CANCEL 7
#define UI_ID_ENTRY_BASE 100
#define UI_ID_TREE_BASE 300

#define MENU_FILE 1
#define MENU_EDIT 2
#define MENU_VIEW 3
#define MENU_TOOLS 4
#define MENU_HELP 5

#define MENU_OPEN 101
#define MENU_NEW 102
#define MENU_DELETE 103
#define MENU_EXIT 104
#define MENU_UP 201
#define MENU_REFRESH 202
#define MENU_DETAILS 301
#define MENU_OPTIONS 402
#define MENU_ABOUT 403

#define CONTEXT_OPEN 501
#define CONTEXT_NEW 502
#define CONTEXT_DELETE 503
#define CONTEXT_UP 504

#define WIN98_BLUE 0x00000080u
#define WIN98_WHITE 0x00FFFFFFu
#define WIN98_GREY 0x00C0C0C0u
#define WIN98_SHADOW 0x00808080u
#define WIN98_TEXT 0x00000000u
#define WIN98_SELECT 0x00000080u

struct file_entry {
    char name[MAX_NAME];
    int is_dir;
};

APP_INFO(APP_TYPE_GUI, "Hermes File Explorer");

static char cwd[MAX_PATH];
static struct file_entry entries[MAX_ENTRIES];
static int entry_count;
static int selected;
static int scroll;

static int modal_action;
static int modal_target_is_dir;
static char text_input[TEXT_INPUT_CAP];
static int text_input_len;
static char status_text[STATUS_CAP];
static int left_shift_held;
static int right_shift_held;
static struct ui_menu_state action_menu_state;
static struct ui_menu_state context_menu_state;

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

/* Hermes deliberately draws its own small Explorer icons.  This keeps the
 * program self-contained (and readable at the system's 8 px font scale)
 * while giving folders and files a much stronger visual identity than the
 * old [D]/[F] prefixes. */
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

static size_t bounded_strlen(const char *text, size_t max) {
    size_t length = 0;
    if (!text)
        return 0;
    while (length < max && text[length])
        length++;
    return length;
}

static int copy_string(char *dst, size_t capacity, const char *src) {
    if (!dst || capacity == 0 || !src)
        return -1;
    size_t length = bounded_strlen(src, capacity);
    if (length >= capacity)
        return -1;
    memcpy(dst, src, length + 1);
    return 0;
}

static void set_status(const char *message) {
    if (copy_string(status_text, sizeof(status_text), message) != 0)
        status_text[0] = 0;
}

static int join_path(char *dst, size_t capacity, const char *dir,
                     const char *name) {
    if (!dst || capacity == 0 || !dir || !name)
        return -1;

    size_t dir_len = bounded_strlen(dir, capacity);
    size_t name_len = bounded_strlen(name, MAX_NAME);
    if (dir_len >= capacity || name_len >= MAX_NAME)
        return -1;

    int needs_slash = dir_len == 0 || dir[dir_len - 1] != '/';
    size_t required = dir_len + (size_t)needs_slash + name_len + 1;
    if (required > capacity)
        return -1;

    memcpy(dst, dir, dir_len);
    size_t offset = dir_len;
    if (needs_slash)
        dst[offset++] = '/';
    memcpy(dst + offset, name, name_len);
    dst[offset + name_len] = 0;
    return 0;
}

static void clamp_selection(void) {
    if (entry_count <= 0) {
        selected = 0;
        scroll = 0;
        return;
    }
    if (selected < 0)
        selected = 0;
    if (selected >= entry_count)
        selected = entry_count - 1;
    if (scroll < 0)
        scroll = 0;
    if (scroll > selected)
        scroll = selected;
}

static int load_entries(void) {
    entry_count = 0;

    copy_string(entries[entry_count].name, MAX_NAME, "..");
    entries[entry_count].is_dir = 1;
    entry_count++;

    unsigned index = 0;
    char buffer[1024];
    long result = 0;

    while (entry_count < MAX_ENTRIES) {
        result = readdir_path(cwd, &index, buffer, sizeof(buffer));
        if (result <= 0)
            break;

        long offset = 0;
        while (offset < result && entry_count < MAX_ENTRIES) {
            const char *name = buffer + offset;
            size_t remaining = (size_t)(result - offset);
            size_t length = bounded_strlen(name, remaining);
            if (length == remaining) {
                set_status("Directory returned a malformed entry");
                clamp_selection();
                return -1;
            }
            offset += (long)length + 1;
            if (length == 0)
                continue;

            int directory = name[length - 1] == '/';
            size_t display_len = directory ? length - 1 : length;
            if (display_len == 0 || display_len >= MAX_NAME)
                continue;
            if (display_len == 1 && name[0] == '.')
                continue;
            if (display_len == 2 && name[0] == '.' && name[1] == '.')
                continue;

            memcpy(entries[entry_count].name, name, display_len);
            entries[entry_count].name[display_len] = 0;
            entries[entry_count].is_dir = directory;
            entry_count++;
        }
    }

    clamp_selection();
    if (result < 0) {
        set_status("Could not read this directory");
        return -1;
    }
    if (entry_count == MAX_ENTRIES)
        set_status("Directory list truncated");
    return 0;
}

static void go_up(void) {
    size_t length = bounded_strlen(cwd, sizeof(cwd));
    if (length <= 1)
        return;

    while (length > 1 && cwd[length - 1] == '/')
        cwd[--length] = 0;
    while (length > 1 && cwd[length - 1] != '/')
        length--;

    if (length <= 1) {
        cwd[0] = '/';
        cwd[1] = 0;
    } else {
        cwd[length - 1] = 0;
    }
    selected = 0;
    scroll = 0;
    load_entries();
}

static int selected_valid(void) {
    return selected >= 0 && selected < entry_count;
}

static void enter_selected_directory(void) {
    if (!selected_valid() || !entries[selected].is_dir)
        return;
    if (selected == 0) {
        go_up();
        return;
    }

    char path[MAX_PATH];
    if (join_path(path, sizeof(path), cwd, entries[selected].name) != 0 ||
        copy_string(cwd, sizeof(cwd), path) != 0) {
        set_status("Path is too long");
        return;
    }
    selected = 0;
    scroll = 0;
    load_entries();
}

static void execute_selected(void) {
    if (!selected_valid() || selected == 0 || entries[selected].is_dir)
        return;

    char path[MAX_PATH];
    if (join_path(path, sizeof(path), cwd, entries[selected].name) != 0) {
        set_status("Path is too long");
        return;
    }

    char *argv[] = {entries[selected].name, 0};
    if (spawn(path, argv) < 0)
        set_status("Could not launch the selected file");
    else
        set_status("Program launch queued");
}

static void activate_selected(void) {
    if (!selected_valid())
        return;
    if (entries[selected].is_dir)
        enter_selected_directory();
    else
        execute_selected();
}

static void open_mkdir_modal(void);
static void open_delete_modal(void);

static void run_menu_action(int action) {
    switch (action) {
    case MENU_OPEN:
    case CONTEXT_OPEN:
        activate_selected();
        break;
    case MENU_NEW:
    case CONTEXT_NEW:
        open_mkdir_modal();
        break;
    case MENU_DELETE:
    case CONTEXT_DELETE:
        open_delete_modal();
        break;
    case MENU_UP:
    case CONTEXT_UP:
        go_up();
        break;
    case MENU_REFRESH:
    case MENU_DETAILS:
        load_entries();
        set_status(action == MENU_DETAILS ? "Details view" : "Folder refreshed");
        break;
    case MENU_ABOUT:
        set_status("Hermes File Explorer");
        break;
    case MENU_OPTIONS:
        set_status("Folder options");
        break;
    default:
        break;
    }
}

static void open_mkdir_modal(void) {
    modal_action = MODAL_MKDIR;
    text_input[0] = 0;
    text_input_len = 0;
}

static void open_delete_modal(void) {
    if (!selected_valid() || selected == 0)
        return;
    if (copy_string(text_input, sizeof(text_input), entries[selected].name) !=
        0) {
        set_status("Name is too long");
        return;
    }
    text_input_len = (int)bounded_strlen(text_input, sizeof(text_input));
    modal_target_is_dir = entries[selected].is_dir;
    modal_action = MODAL_DELETE;
}

static void cancel_modal(void) {
    modal_action = MODAL_NONE;
    text_input[0] = 0;
    text_input_len = 0;
}

static void confirm_modal(void) {
    if (modal_action == MODAL_NONE)
        return;
    text_input[text_input_len] = 0;
    if (text_input_len == 0) {
        set_status("A name is required");
        return;
    }

    char path[MAX_PATH];
    if (join_path(path, sizeof(path), cwd, text_input) != 0) {
        set_status("Path is too long");
        return;
    }

    long result;
    if (modal_action == MODAL_MKDIR) {
        result = mkdir_path(path);
        set_status(result == 0 ? "Directory created" :
                                 "Could not create directory");
    } else {
        result = modal_target_is_dir ? rmdir_path(path) : unlink(path);
        if (result == 0)
            set_status(modal_target_is_dir ? "Directory deleted" :
                                             "File deleted");
        else
            set_status(modal_target_is_dir ?
                           "Directory is not empty or could not be deleted" :
                           "Could not delete file");
    }

    if (result == 0) {
        modal_action = MODAL_NONE;
        load_entries();
    }
}

static int handle_key(int key, int pressed, int *running) {
    if (key == KEY_LEFTSHIFT) {
        left_shift_held = pressed;
        return 1;
    }
    if (key == KEY_RIGHTSHIFT) {
        right_shift_held = pressed;
        return 1;
    }
    if (!pressed)
        return 0;

    if (modal_action != MODAL_NONE) {
        if (key == KEY_ESC) {
            cancel_modal();
            return 1;
        }
        if (key == KEY_ENTER || key == KEY_KPENTER) {
            confirm_modal();
            return 1;
        }
        if (modal_action == MODAL_DELETE)
            return 0;
        if (key == KEY_BACKSPACE) {
            if (text_input_len > 0)
                text_input[--text_input_len] = 0;
            return 1;
        }

        char character = keymap_to_ascii(
            (uint16_t)key, left_shift_held || right_shift_held);
        if (character >= 32 && character <= 126 && character != '/' &&
            character != '\\' && text_input_len + 1 < TEXT_INPUT_CAP) {
            text_input[text_input_len++] = character;
            text_input[text_input_len] = 0;
            return 1;
        }
        return 0;
    }

    if (key == KEY_ESC) {
        if (action_menu_state.open || context_menu_state.open) {
            action_menu_state.open = 0;
            context_menu_state.open = 0;
            return 1;
        }
        *running = 0;
        return 1;
    }
    if (key == KEY_UP) {
        if (selected > 0)
            selected--;
        return 1;
    }
    if (key == KEY_DOWN) {
        if (selected + 1 < entry_count)
            selected++;
        return 1;
    }
    if (key == KEY_ENTER || key == KEY_KPENTER) {
        activate_selected();
        return 1;
    }
    if (key == KEY_BACKSPACE || key == KEY_LEFT) {
        go_up();
        return 1;
    }

    char character = keymap_to_ascii(
        (uint16_t)key, left_shift_held || right_shift_held);
    if (character == 'q' || character == 'Q') {
        *running = 0;
        return 1;
    }
    if (character == 'k' || character == 'w') {
        if (selected > 0)
            selected--;
        return 1;
    }
    if (character == 'j' || character == 's') {
        if (selected + 1 < entry_count)
            selected++;
        return 1;
    }
    if (character == 'h') {
        go_up();
        return 1;
    }
    return 0;
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

static int path_depth(void) {
    int depth = 0;
    int in_component = 0;
    for (const char *p = cwd; *p; p++) {
        if (*p == '/') {
            in_component = 0;
        } else if (!in_component) {
            depth++;
            in_component = 1;
        }
    }
    return depth;
}

/* Return a component of cwd for the tree label.  The explorer tree is not a
 * second directory cache: its expanded branch always mirrors the directory
 * that Hermes is actually showing. */
static const char *path_component(int wanted, char *out, size_t capacity) {
    int component = 0;
    const char *p = cwd;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        const char *start = p;
        while (*p && *p != '/')
            p++;
        if (component++ == wanted) {
            size_t length = (size_t)(p - start);
            if (length >= capacity)
                length = capacity - 1;
            memcpy(out, start, length);
            out[length] = 0;
            return out;
        }
    }
    out[0] = 0;
    return out;
}

static void go_to_tree_depth(int depth) {
    if (depth <= 0) {
        copy_string(cwd, sizeof(cwd), "/");
    } else {
        char path[MAX_PATH] = "/";
        char next[MAX_PATH];
        char component[MAX_NAME];
        for (int i = 0; i < depth; i++) {
            path_component(i, component, sizeof(component));
            if (!component[0] || join_path(next, sizeof(next), path,
                                           component) != 0 ||
                copy_string(path, sizeof(path), next) != 0) {
                set_status("Path is too long");
                return;
            }
        }
        copy_string(cwd, sizeof(cwd), path);
    }
    selected = 0;
    scroll = 0;
    load_entries();
}

static int is_program_name(const char *name) {
    size_t length = bounded_strlen(name, MAX_NAME);
    return length > 4 && name[length - 4] == '.' &&
           (name[length - 3] == 'e' || name[length - 3] == 'E') &&
           (name[length - 2] == 'l' || name[length - 2] == 'L') &&
           (name[length - 1] == 'f' || name[length - 1] == 'F');
}

static void draw_tree_row(struct ui_context *ui, struct gfx_surface *surface,
                          int id, struct gfx_rect row, int indent,
                          const char *label, int selected_row, int folder,
                          int expandable, int expanded) {
    /* ui_button_id supplies reliable press/release tracking.  The custom
     * paint after it is intentional: Explorer rows are flat, not buttons. */
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
    gfx_text_box(surface, gfx_rect_make(x + 18, row.y, row.w - (x - row.x) - 20,
                                        row.h), label,
                 selected_row ? WIN98_WHITE : WIN98_TEXT, 1, 0,
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
        /* Same interaction technique as the tree: keep selection rows flat. */
        int clicked = ui_button_id(ui, UI_ID_ENTRY_BASE + index, row, "");
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
    }
    return changed;
}

static int draw_frame(struct ui_context *ui, struct gfx_surface *surface,
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

    /* Explorer's identifying structure: menu + toolbars, address bar, a
     * left navigation tree, and a white Details view on the right. */
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

    /* Menus are rendered last so their dropdowns sit above the Explorer
     * content, just like a native desktop menu. */
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

int main(void) {
    if (!getcwd(cwd, sizeof(cwd)) || cwd[0] == 0)
        copy_string(cwd, sizeof(cwd), "/");
    set_status("Ready");
    load_entries();

    struct wm_window window;
    if (wm_window_create(WINDOW_W, WINDOW_H, "Hermes", &window) != 0) {
        printf("hermes: could not create window\n");
        return 1;
    }

    struct gfx_surface surface;
    gfx_surface_init(&surface, (uint32_t *)(uintptr_t)window.surface_va,
                     window.w, window.h, (int)(window.pitch / 4));
    printf("hermes: ready handle=%d cwd=%s\n", window.handle, cwd);
    struct ui_context ui;
    memset(&ui, 0, sizeof(ui));

    int mouse_x = 0;
    int mouse_y = 0;
    int buttons = 0;
    int running = 1;
    int redraw = 1;

    while (running) {
        int saw_event = 0;
        int button_edges = 0;
        struct wm_event event;
        while (button_edges < 1 && wm_poll_event(&event)) {
            saw_event = 1;
            switch (event.type) {
            case WM_EV_KEY_DOWN:
                redraw |= handle_key(event.param, 1, &running);
                break;
            case WM_EV_KEY_UP:
                redraw |= handle_key(event.param, 0, &running);
                break;
            case WM_EV_MOUSE_MOVE:
                mouse_x = event.x;
                mouse_y = event.y;
                redraw = 1;
                break;
            case WM_EV_MOUSE_DOWN:
                mouse_x = event.x;
                mouse_y = event.y;
                buttons |= event.param;
                button_edges++;
                redraw = 1;
                break;
            case WM_EV_MOUSE_UP:
                mouse_x = event.x;
                mouse_y = event.y;
                buttons &= ~event.param;
                button_edges++;
                redraw = 1;
                break;
            case WM_EV_RESIZE:
                window.surface_va = event.surface_va;
                window.pitch = event.pitch;
                window.w = event.w;
                window.h = event.h;
                gfx_surface_init(&surface,
                                 (uint32_t *)(uintptr_t)window.surface_va,
                                 window.w, window.h,
                                 (int)(window.pitch / 4));
                memset(&ui, 0, sizeof(ui));
                redraw = 1;
                break;
            case WM_EV_QUIT:
                running = 0;
                break;
            default:
                break;
            }
        }

        if (!running)
            break;
        if (!redraw && !saw_event) {
            sleep_ticks(1);
            continue;
        }

        ui_begin(&ui, &surface, &ui_theme_default, mouse_x, mouse_y, buttons);
        int changed = draw_frame(&ui, &surface, &running);
        ui_end(&ui);
        wm_window_invalidate(window.handle);
        redraw = changed;
        yield();
    }

    wm_window_destroy(window.handle);
    printf("hermes: exit\n");
    return 0;
}
