#define _GNU_SOURCE
#include "tsearch.h"
#include <search.h>
#include <stdlib.h>

void tdestroy(void *root, void (*freekey)(void *)) {
  struct node *r = root;

  if (r == 0)
    return;
  tdestroy(r->a[0], freekey);
  tdestroy(r->a[1], freekey);
  if (freekey)
    freekey((void *)r->key);
  free(r);
}
