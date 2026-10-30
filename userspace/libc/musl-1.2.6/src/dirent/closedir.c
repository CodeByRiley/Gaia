#include "__dirent.h"
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

int closedir(DIR *dir) {
  int ret = close(dir->fd);
  free(dir);
  return ret;
}
