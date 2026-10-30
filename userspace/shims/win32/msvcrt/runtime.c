#include <shims/win32/windows.h>
void *memcpy(void *, const void *, size_t);
size_t strlen(const char *);
void *malloc(size_t n) { return HeapAlloc(GetProcessHeap(), 0, n); }
void *calloc(size_t n, size_t size) {
  if (size && n > SIZE_MAX / size)
    return 0;
  return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n * size);
}
void free(void *p) { HeapFree(GetProcessHeap(), 0, p); }
void *realloc(void *p, size_t n) {
  if (!n) {
    free(p);
    return 0;
  }
  return HeapReAlloc(GetProcessHeap(), 0, p, n);
}
int puts(const char *s) {
  DWORD n, k;
  if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &n, 0))
    return -1;
  return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "\n", 1, &k, 0) ? 0 : -1;
}
void exit(int code) { ExitProcess((UINT)code); }
int _write(int fd, const void *p, unsigned count) {
  DWORD n;
  HANDLE h = GetStdHandle(fd == 2 ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
  return WriteFile(h, p, count, &n, 0) ? (int)n : -1;
}
