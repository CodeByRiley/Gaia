#ifndef SKALD_CONSOLE_H
#define SKALD_CONSOLE_H

#define printf sh_printf
#define con_out(s, n) console_write((s), (n))

int sh_printf(const char *fmt, ...);

#endif
