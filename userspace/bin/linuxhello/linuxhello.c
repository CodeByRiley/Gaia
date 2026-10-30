/* Built by the host Linux toolchain as an ordinary static ELF at its default
 * address. No Gaia headers, libc patches, or custom linker script. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void) {
  puts("linuxhello: unmodified static Linux ELF");
  int fd = open("/linux-test.txt", O_CREAT | O_TRUNC | O_RDWR, 0600);
  if (fd < 0)
    return 1;
  if (write(fd, "Linux", 5) != 5 || lseek(fd, 0, SEEK_SET) != 0)
    return 2;
  char text[6] = {0};
  if (read(fd, text, 5) != 5 || strcmp(text, "Linux"))
    return 3;
  close(fd);
  char *p = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED)
    return 4;
  p[4096] = 42;
  if (munmap(p, 8192))
    return 5;
  p = malloc(1234);
  if (!p)
    return 6;
  memset(p, 123, 1234);
  free(p);
  puts("linuxhello: all checks passed");
  return 0;
}
