/* tests/sdk_smoke/main.c , a program built only from the installed SDK.
 *
 * tests/sdk_smoke_test.py copies this directory out of the Gaia tree and
 * builds it there with nothing but x86_64-unknown-gaia-gcc and -lgaia, so it
 * proves the installed headers and archive stand on their own. It then boots
 * Gaia and runs it.
 *
 * Run without arguments it re-launches itself through process_exec() with
 * "child", which exits 42; the parent checks that code came back. That
 * exercises PATH lookup, the exec syscall, and exit-code propagation, and
 * pulls process.o and syscall.o out of libgaia.a rather than only musl.
 */
#include <gaia/gaia.h>
#include <stdio.h>
#include <string.h>

#define CHILD_EXIT 42

int main(int argc, char **argv) {
  if (argc > 1 && strcmp(argv[1], "child") == 0)
    return CHILD_EXIT;

  char path[PROCESS_PATH_MAX];
  if (process_resolve("sdksmoke", NULL, path, sizeof(path)) != 0) {
    printf("gaia sdk smoke: cannot resolve sdksmoke on %s\n", PROCESS_DEFAULT_PATH);
    return 1;
  }

  char *const child_argv[] = {"sdksmoke", "child", NULL};
  long status              = process_exec("sdksmoke", child_argv, NULL);
  if (status != CHILD_EXIT) {
    printf("gaia sdk smoke: child at %s returned %ld, want %d\n", path, status, CHILD_EXIT);
    return 1;
  }

  printf("gaia sdk smoke: ok (pid %ld, abi %d, child %s returned %ld)\n", get_pid(),
         GAIA_SDK_ABI_VERSION, path, status);
  return 0;
}
