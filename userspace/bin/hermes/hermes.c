/* userspace/bin/hermes/hermes.c - Hermes File Explorer.
 *
 * Traverses FAT32/16 directory entries and manages file/folder operations.
 * Operates visually alongside the SH(ell).ELF prompt, listening for keyboard
 * events to navigate directory trees, launch executables via spawn(), and
 * organize the local volume.
 *
 */

#include "hermes_prototypes.h"
#include <lib/keymap.h>
#include <lib/syscall.h>
#include <lib/app_info.h>
#include <lib/wm.h>
#include <include/key_codes.h>
#include <stdio.h>
#include <string.h>

APP_INFO(APP_TYPE_GUI, "Hermes File Explorer");

char cwd[MAX_PATH];
struct file_entry entries[MAX_ENTRIES];
int entry_count;
int selected;
int scroll;
int modal_action;
int modal_target_is_dir;
char text_input[TEXT_INPUT_CAP];
int text_input_len;
char status_text[STATUS_CAP];
int left_shift_held;
int right_shift_held;
struct ui_menu_state action_menu_state;
struct ui_menu_state context_menu_state;

size_t bounded_strlen(const char *text, size_t max) {
    size_t length = 0;
    if (!text)
        return 0;
    while (length < max && text[length])
        length++;
    return length;
}

int copy_string(char *dst, size_t capacity, const char *src) {
    if (!dst || capacity == 0 || !src)
        return -1;
    size_t length = bounded_strlen(src, capacity);
    if (length >= capacity)
        return -1;
    memcpy(dst, src, length + 1);
    return 0;
}

void set_status(const char *message) {
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

int load_entries(void) {
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

void go_up(void) {
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

int selected_valid(void) {
    return selected >= 0 && selected < entry_count;
}

void enter_selected_directory(void) {
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

void execute_selected(void) {
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

void activate_selected(void) {
    if (!selected_valid())
        return;
    if (entries[selected].is_dir)
        enter_selected_directory();
    else
        execute_selected();
}

void open_mkdir_modal(void);
void open_delete_modal(void);

void run_menu_action(int action) {
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

int path_depth(void) {
    int depth = 0;
    int in_component = 0;
    for (const char *p = cwd; *p; p++) {
        if (*p == '/')
            in_component = 0;
        else if (!in_component) {
            depth++;
            in_component = 1;
        }
    }
    return depth;
}

const char *path_component(int wanted, char *out, size_t capacity) {
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

void go_to_tree_depth(int depth) {
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

int is_program_name(const char *name) {
    size_t length = bounded_strlen(name, MAX_NAME);
    return length > 4 && name[length - 4] == '.' &&
           (name[length - 3] == 'e' || name[length - 3] == 'E') &&
           (name[length - 2] == 'l' || name[length - 2] == 'L') &&
           (name[length - 1] == 'f' || name[length - 1] == 'F');
}

void open_mkdir_modal(void) {
    modal_action = MODAL_MKDIR;
    text_input[0] = 0;
    text_input_len = 0;
}

void open_delete_modal(void) {
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

void cancel_modal(void) {
    modal_action = MODAL_NONE;
    text_input[0] = 0;
    text_input_len = 0;
}

void confirm_modal(void) {
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

int handle_key(int key, int pressed, int *running) {
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
