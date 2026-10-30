#include "getc.h"
#include <stdio.h>

int getc(FILE *f) { return do_getc(f); }

weak_alias(getc, _IO_getc);
