/* Flush every mounted filesystem and its transport cache. */
#include <errno.h>
#include <lib/syscall.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
  (void)argv;
  if (argc != 1) {
    printf("usage: sync\n");
    return 2;
  }
  if (fs_sync() != 0) {
    printf("sync: filesystem flush failed: %s\n", strerror(errno));
    return 1;
  }
  return 0;
}
