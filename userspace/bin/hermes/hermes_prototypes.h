/* Private interface shared by Hermes' behavior and renderer modules. */
#ifndef HERMES_PROTOTYPES_H
#define HERMES_PROTOTYPES_H

#include <lib/gfx.h>
#include <lib/ui.h>
#include <stddef.h>

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
#define MODAL_OPEN_WITH 3

#define UI_ID_UP 1
#define UI_ID_OPEN 2
#define UI_ID_MKDIR 3
#define UI_ID_DELETE 4
#define UI_ID_QUIT 5
#define UI_ID_CONFIRM 6
#define UI_ID_CANCEL 7
#define UI_ID_OPEN_WITH_NOTEPAD 8
#define UI_ID_OPEN_WITH_CAT 9
#define UI_ID_OPEN_WITH_CANCEL 10
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
#define MENU_OPEN_WITH 105
#define MENU_UP 201
#define MENU_REFRESH 202
#define MENU_DETAILS 301
#define MENU_OPTIONS 402
#define MENU_ABOUT 403

#define CONTEXT_OPEN 501
#define CONTEXT_NEW 502
#define CONTEXT_DELETE 503
#define CONTEXT_UP 504
#define CONTEXT_OPEN_WITH 505

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

extern char cwd[MAX_PATH];
extern struct file_entry entries[MAX_ENTRIES];
extern int entry_count;
extern int selected;
extern int scroll;
extern int modal_action;
extern int modal_target_is_dir;
extern char text_input[TEXT_INPUT_CAP];
extern int text_input_len;
extern char status_text[STATUS_CAP];
extern int left_shift_held;
extern int right_shift_held;
extern struct ui_menu_state action_menu_state;
extern struct ui_menu_state context_menu_state;

size_t bounded_strlen(const char *text, size_t max);
int copy_string(char *dst, size_t capacity, const char *src);
void set_status(const char *message);
int load_entries(void);
void go_up(void);
int selected_valid(void);
void enter_selected_directory(void);
void execute_selected(void);
void activate_selected(void);
void open_with_selected(void);
void launch_selected_with(const char *program, const char *label);
void open_mkdir_modal(void);
void open_delete_modal(void);
void cancel_modal(void);
void confirm_modal(void);
void run_menu_action(int action);
int handle_key(int key, int pressed, int *running);
int path_depth(void);
const char *path_component(int wanted, char *out, size_t capacity);
void go_to_tree_depth(int depth);
int is_program_name(const char *name);

int draw_frame(struct ui_context *ui, struct gfx_surface *surface,
               int *running);

#endif
