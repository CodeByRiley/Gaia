#include "putc.h"
#include <stdio.h>

int putc(int c, FILE *f) { return do_putc(c, f); }

weak_alias(putc, _IO_putc);
