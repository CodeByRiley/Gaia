/* userspace/bin/chmod/chmod.c , change file permission bits.
 *
 *   chmod MODE FILE...
 *
 * MODE is octal (755, 0644) or a comma-separated list of symbolic clauses
 * in the POSIX form [ugoa]*[+-=][rwxXst]*, e.g. u+x, go-w, a=r,u+w. With
 * no [ugoa] the clause applies to all three classes minus the umask, as
 * POSIX specifies. X adds execute only for directories or files that
 * already have an execute bit.
 *
 * Uses standard headers only: lib/app_info.h pulls in libgaia's
 * sys/types.h, which clashes with musl's <sys/stat.h>.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int parse_octal(const char *text, mode_t *out) {
  mode_t mode = 0;
  if (!*text)
    return -1;
  for (const char *p = text; *p; p++) {
    if (*p < '0' || *p > '7')
      return -1;
    mode = (mode << 3) | (mode_t)(*p - '0');
    if (mode > 07777)
      return -1;
  }
  *out = mode;
  return 0;
}

/* Apply the symbolic MODE to `mode` (the file's current bits). Returns
 * -1 on a syntax error. */
static int apply_symbolic(const char *text, mode_t mode, int is_dir, mode_t umask_bits,
                          mode_t *out) {
  const char *p = text;
  for (;;) {
    mode_t who = 0;
    for (;; p++) {
      if (*p == 'u')
        who |= S_ISUID | S_IRWXU;
      else if (*p == 'g')
        who |= S_ISGID | S_IRWXG;
      else if (*p == 'o')
        who |= S_IRWXO;
      else if (*p == 'a')
        who |= S_ISUID | S_ISGID | S_IRWXU | S_IRWXG | S_IRWXO;
      else
        break;
    }
    int explicit_who = who != 0;
    if (!explicit_who)
      who = S_ISUID | S_ISGID | S_IRWXU | S_IRWXG | S_IRWXO;

    if (*p != '+' && *p != '-' && *p != '=')
      return -1;
    while (*p == '+' || *p == '-' || *p == '=') {
      char op = *p++;
      mode_t perm = 0;
      for (; *p && *p != ',' && *p != '+' && *p != '-' && *p != '='; p++) {
        switch (*p) {
        case 'r':
          perm |= S_IRUSR | S_IRGRP | S_IROTH;
          break;
        case 'w':
          perm |= S_IWUSR | S_IWGRP | S_IWOTH;
          break;
        case 'x':
          perm |= S_IXUSR | S_IXGRP | S_IXOTH;
          break;
        case 'X':
          if (is_dir || (mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
            perm |= S_IXUSR | S_IXGRP | S_IXOTH;
          break;
        case 's':
          perm |= S_ISUID | S_ISGID;
          break;
        case 't':
          perm |= S_ISVTX;
          break;
        default:
          return -1;
        }
      }
      /* The sticky bit belongs to no class; let it through whenever the
       * clause names it, and drop the umask only when no class was given. */
      mode_t mask = who | (perm & S_ISVTX);
      perm &= mask;
      if (!explicit_who)
        perm &= ~umask_bits;
      if (op == '+')
        mode |= perm;
      else if (op == '-')
        mode &= ~perm;
      else
        mode = (mode & ~who) | perm;
    }
    if (*p == '\0')
      break;
    if (*p != ',')
      return -1;
    p++;
  }
  *out = mode;
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: chmod MODE FILE...\n");
    return 2;
  }

  mode_t octal;
  int is_octal = parse_octal(argv[1], &octal) == 0;
  mode_t umask_bits = umask(0);
  umask(umask_bits);

  int status = 0;
  for (int i = 2; i < argc; i++) {
    mode_t mode = octal;
    if (!is_octal) {
      struct stat st;
      if (stat(argv[i], &st) != 0) {
        perror(argv[i]);
        status = 1;
        continue;
      }
      if (apply_symbolic(argv[1], st.st_mode & 07777, S_ISDIR(st.st_mode), umask_bits,
                         &mode) != 0) {
        fprintf(stderr, "chmod: invalid mode: %s\n", argv[1]);
        return 2;
      }
    }
    if (chmod(argv[i], mode) != 0) {
      perror(argv[i]);
      status = 1;
    }
  }
  return status;
}
