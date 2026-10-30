#include <shims/win32/handles.h>
int GaiaPath(const char *in, char *out, size_t cap) {
  if (!in || !out || !cap)
    return -1;
  size_t n = 0;
  if (in[0] && in[1] == ':') {
    if (in[0] != 'C' && in[0] != 'c')
      return -1;
    in += 2;
    if (*in != '/' && *in != '\\')
      return -1;
  }
  for (; *in; in++) {
    if (n + 1 >= cap)
      return -1;
    out[n++] = *in == '\\' ? '/' : *in;
  }
  out[n] = 0;
  return n ? 0 : -1;
}
