/* userspace/bin/cat/cat.c , dump one or more files, or stdin, to stdout.
 *
 * The first binary that was built against musl rather than userspace/lib,
 * and still the smallest one that touches startup, stdio and the syscall
 * layer at once , so it stays the first thing to check when a musl or
 * kernel change breaks userspace. Uses standard headers only: no
 * <lib/syscall.h>, no hand-declared externs.
 */
#include <stdio.h>
#include <unistd.h>
#include <lib/app_info.h>

APP_INFO(APP_TYPE_CLI, "cat");
static int cat_one(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror(path);
        return 1;
    }

    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (write(1, buf, n) < 0) {
            perror("cat: write");
            break;
        }
    }
    fclose(fp);
    return 0;
}

/* No files: copy stdin, which is what makes cat the end of a pipeline. A
 * terminal on stdin still gets the usage line , console reads do not
 * block, so cat there would just exit silently. */
static int cat_stdin(void) {
    char buf[256];
    long n;
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        if (write(1, buf, (size_t)n) < 0) {
            perror("cat: write");
            return 1;
        }
    }
    return n < 0 ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        if (!isatty(0))
            return cat_stdin();
        printf("usage: cat FILE...\n");
        return 1;
    }

    int failed = 0;
    for (int i = 1; i < argc; i++)
        failed |= cat_one(argv[i]);
    return failed ? 1 : 0;
}
