#include <shims/win32/handles.h>
int GaiaUtf8(const WCHAR *in, char *out, size_t cap) {
  if (!in || !out || !cap)
    return -1;
  size_t n = 0;
  for (size_t i = 0; in[i]; i++) {
    uint32_t c = in[i];
    if (c >= 0xd800 && c <= 0xdbff) {
      uint32_t b = in[++i];
      if (b < 0xdc00 || b > 0xdfff)
        return -1;
      c = 0x10000 + ((c - 0xd800) << 10) + (b - 0xdc00);
    } else if (c >= 0xdc00 && c <= 0xdfff)
      return -1;
    unsigned k = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
    if (n + k >= cap)
      return -1;
    if (k == 1)
      out[n++] = c;
    else {
      out[n++] = (k == 2 ? 0xc0 : k == 3 ? 0xe0 : 0xf0) | (c >> (6 * (k - 1)));
      for (unsigned j = k - 1; j; j--)
        out[n++] = 0x80 | ((c >> (6 * (j - 1))) & 63);
    }
  }
  out[n] = 0;
  return (int)n;
}
int GaiaUtf16(const char *in, WCHAR *out, size_t cap) {
  if (!in || !out || !cap)
    return -1;
  size_t n = 0;
  for (size_t i = 0; in[i];) {
    uint32_t c = (unsigned char)in[i++];
    unsigned extra = 0;
    uint32_t min = 0;
    if (c >= 0xc2 && c <= 0xdf) {
      c &= 31;
      extra = 1;
      min = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
      c &= 15;
      extra = 2;
      min = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
      c &= 7;
      extra = 3;
      min = 0x10000;
    } else if (c >= 0x80)
      return -1;
    while (extra--) {
      unsigned char b = in[i++];
      if ((b & 0xc0) != 0x80)
        return -1;
      c = (c << 6) | (b & 63);
    }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
      return -1;
    if (n + (c > 0xffff ? 2 : 1) >= cap)
      return -1;
    if (c > 0xffff) {
      c -= 0x10000;
      out[n++] = 0xd800 + (c >> 10);
      out[n++] = 0xdc00 + (c & 1023);
    } else
      out[n++] = c;
  }
  out[n] = 0;
  return (int)n;
}
