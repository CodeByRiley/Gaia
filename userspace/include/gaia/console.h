/* <gaia/console.h> , the per-process console (TTY).
 *
 * A program's console is the TTY channel it inherited at spawn; heimdall
 * renders it in a console window. Output written here, or to fd 1 through
 * musl, lands in the same place. These calls add what POSIX stdio has no
 * word for: clearing, an alternate screen, glyph zoom, and reading the
 * characters heimdall routed to this console.
 */
#ifndef GAIA_SDK_CONSOLE_H
#define GAIA_SDK_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Raw console syscalls ----------------------------------------------- */

/* Write `n` bytes to the console. Returns the count written or a negative
 * value. */
long con_write(const char *buf, size_t n);
/* Erase the visible console. */
long con_clear(void);
/* Push the current screen and cursor on a one-deep stack and clear; pop
 * restores it. The pair gives fullscreen programs an alternate screen. */
long con_push(void);
long con_pop(void);
/* Change the glyph scale by `delta` steps. */
long con_zoom(long delta);

/* Drain characters heimdall routed to this process's console. Non-blocking;
 * returns how many were read, 0 when nothing is queued.
 *
 * Read keyboard text here rather than polling the raw keyboard: the raw ring
 * collects every keystroke regardless of which window has focus. */
long tty_read_input(char *buf, unsigned long max);

/* ---- Convenience wrappers ----------------------------------------------- */

void console_init(void);
void console_clear(void);
void console_zoom_in(void);
void console_zoom_out(void);
void console_putc(char c);
void console_puts(const char *s);
void console_write(const char *buf, size_t len);
/* No effect: colour is chosen by the console renderer, not the client.
 * Use ANSI escape sequences in the output stream instead. */
void console_set_color(uint32_t fg, uint32_t bg);
/* con_push()/con_pop() narrowed to int. */
int console_save(void);
int console_restore(void);

#ifdef __cplusplus
}
#endif

#endif
