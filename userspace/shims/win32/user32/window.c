#include <include/key_codes.h>
#include <lib/keymap.h>
#include <lib/services.h>
#include <shims/win32/bridge.h>
#include <shims/win32/handles.h>
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
int strcmp(const char *, const char *);
size_t strlen(const char *);
struct cls {
  char name[64];
  WNDPROC procedure;
  ATOM atom;
};
static struct cls classes[16];
struct win {
  struct gaia_dc dc;
  HWND handle;
  WNDPROC procedure;
  unsigned generation;
  int paint;
};
static struct win windows[32];
static MSG queue[64];
static unsigned count;
static struct win *lookup(HWND h) {
  for (unsigned i = 0; i < 32; i++)
    if (h && windows[i].handle == h)
      return windows + i;
  return 0;
}
struct gaia_dc *GaiaGetDC(HDC h) {
  struct win *w = lookup(h);
  if (!w) {
    SetLastError(6);
    return 0;
  }
  return &w->dc;
}
void GaiaPresentDC(HDC h) {
  struct win *w = lookup(h);
  if (w)
    wm_window_invalidate(w->dc.window.handle);
}
ATOM WINAPI RegisterClassA(const WNDCLASSA *c) {
  if (!c || !c->lpszClassName || !c->lpfnWndProc || c->cbClsExtra || c->cbWndExtra) {
    SetLastError(87);
    return 0;
  }
  size_t n = strlen(c->lpszClassName);
  if (n >= 64) {
    SetLastError(87);
    return 0;
  }
  for (unsigned i = 0; i < 16; i++)
    if (classes[i].atom && !strcmp(classes[i].name, c->lpszClassName)) {
      SetLastError(1410);
      return 0;
    }
  for (unsigned i = 0; i < 16; i++)
    if (!classes[i].atom) {
      memcpy(classes[i].name, c->lpszClassName, n + 1);
      classes[i].procedure = c->lpfnWndProc;
      classes[i].atom      = i + 1;
      return i + 1;
    }
  SetLastError(8);
  return 0;
}
ATOM WINAPI RegisterClassW(const WNDCLASSW *c) {
  if (!c)
    return 0;
  char name[64];
  if (GaiaUtf8(c->lpszClassName, name, 64) < 0)
    return 0;
  WNDCLASSA a = {c->style,
                 c->lpfnWndProc,
                 c->cbClsExtra,
                 c->cbWndExtra,
                 c->hInstance,
                 c->hIcon,
                 c->hCursor,
                 c->hbrBackground,
                 0,
                 name};
  return RegisterClassA(&a);
}
ATOM WINAPI RegisterClassExA(const WNDCLASSEXA *c) {
  if (!c || c->cbSize != sizeof(*c))
    return 0;
  WNDCLASSA a = {c->style,
                 c->lpfnWndProc,
                 c->cbClsExtra,
                 c->cbWndExtra,
                 c->hInstance,
                 c->hIcon,
                 c->hCursor,
                 c->hbrBackground,
                 c->lpszMenuName,
                 c->lpszClassName};
  return RegisterClassA(&a);
}
BOOL WINAPI PostMessageA(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (h && !lookup(h)) {
    SetLastError(1400);
    return FALSE;
  }
  if (count == 64) {
    SetLastError(8);
    return FALSE;
  }
  queue[count++] = (MSG){.hwnd = h, .message = msg, .wParam = wp, .lParam = lp};
  return TRUE;
}
void WINAPI PostQuitMessage(INT code) { PostMessageA(0, WM_QUIT, (WPARAM)code, 0); }
HWND WINAPI CreateWindowExA(DWORD ex, const char *class_name, const char *title, DWORD style, INT x,
                            INT y, INT width, INT height, HWND parent, HMENU menu,
                            HINSTANCE instance, void *param) {
  if (ex || parent || menu) {
    SetLastError(120);
    return 0;
  }
  struct cls *c  = 0;
  uintptr_t atom = (uintptr_t)class_name;
  for (unsigned i = 0; i < 16; i++)
    if (classes[i].atom && ((atom <= 65535 && classes[i].atom == atom) ||
                            (atom > 65535 && !strcmp(classes[i].name, class_name)))) {
      c = classes + i;
      break;
    }
  if (!c) {
    SetLastError(1407);
    return 0;
  }
  if (width == CW_USEDEFAULT)
    width = 480;
  if (height == CW_USEDEFAULT)
    height = 320;
  if (width <= 0 || height <= 0) {
    SetLastError(87);
    return 0;
  }
  struct win *w = 0;
  unsigned slot = 0;
  for (; slot < 32; slot++)
    if (!windows[slot].handle) {
      w = windows + slot;
      break;
    }
  if (!w) {
    SetLastError(8);
    return 0;
  }
  if (wm_window_create(width, height, title ? title : "", &w->dc.window)) {
    SetLastError(8);
    return 0;
  }
  w->generation++;
  w->handle              = (HWND)(uintptr_t)(((uint64_t)w->generation << 16) | (slot + 1024));
  w->procedure           = c->procedure;
  w->dc.text_color       = 0;
  w->dc.background_color = 0xffffff;
  w->paint               = 1;
  CREATESTRUCTA create   = {
      param, instance, menu, parent, height, width, y, x, (LONG)style, title, class_name, ex};
  if (!w->procedure(w->handle, WM_NCCREATE, 0, (LPARAM)(uintptr_t)&create) ||
      w->procedure(w->handle, WM_CREATE, 0, (LPARAM)(uintptr_t)&create) == -1) {
    DestroyWindow(w->handle);
    return 0;
  }
  PostMessageA(w->handle, WM_PAINT, 0, 0);
  return w->handle;
}
HWND WINAPI CreateWindowExW(DWORD ex, const WCHAR *c, const WCHAR *t, DWORD s, INT x, INT y, INT w,
                            INT h, HWND p, HMENU m, HINSTANCE i, void *arg) {
  char name[64], title[256];
  if ((uintptr_t)c > 65535) {
    if (GaiaUtf8(c, name, 64) < 0)
      return 0;
  }
  if (t) {
    if (GaiaUtf8(t, title, 256) < 0)
      return 0;
  } else
    title[0] = 0;
  return CreateWindowExA(
      ex, (uintptr_t)c <= 65535 ? (const char *)c : name, title, s, x, y, w, h, p, m, i, arg);
}
BOOL WINAPI DestroyWindow(HWND h) {
  struct win *w = lookup(h);
  if (!w) {
    SetLastError(1400);
    return FALSE;
  }
  w->procedure(h, WM_DESTROY, 0, 0);
  w->procedure(h, WM_NCDESTROY, 0, 0);
  wm_window_destroy(w->dc.window.handle);
  w->handle = 0;
  for (unsigned i = 0; i < count;) {
    if (queue[i].hwnd == h) {
      for (unsigned j = i + 1; j < count; j++)
        queue[j - 1] = queue[j];
      count--;
    } else
      i++;
  }
  return TRUE;
}
BOOL WINAPI ShowWindow(HWND h, INT mode) {
  (void)mode;
  return lookup(h) != 0;
}
BOOL WINAPI UpdateWindow(HWND h) {
  struct win *w = lookup(h);
  if (!w)
    return FALSE;
  if (w->paint)
    w->procedure(h, WM_PAINT, 0, 0);
  return TRUE;
}
BOOL WINAPI GetClientRect(HWND h, RECT *out) {
  struct win *w = lookup(h);
  if (!w || !out)
    return FALSE;
  *out = (RECT){0, 0, w->dc.window.w, w->dc.window.h};
  return TRUE;
}
BOOL WINAPI SetWindowTextA(HWND h, const char *s) {
  struct win *w = lookup(h);
  return w && wm_window_set_title(w->dc.window.handle, s) == 0;
}
BOOL WINAPI InvalidateRect(HWND h, const RECT *r, BOOL erase) {
  (void)r;
  (void)erase;
  struct win *w = lookup(h);
  if (!w)
    return FALSE;
  if (!w->paint) {
    w->paint = 1;
    return PostMessageA(h, WM_PAINT, 0, 0);
  }
  return TRUE;
}
HDC WINAPI GetDC(HWND h) { return lookup(h) ? h : 0; }
INT WINAPI ReleaseDC(HWND h, HDC dc) {
  if (h != dc || !lookup(h))
    return 0;
  GaiaPresentDC(dc);
  return 1;
}
HDC WINAPI BeginPaint(HWND h, PAINTSTRUCT *p) {
  struct win *w = lookup(h);
  if (!w || !p)
    return 0;
  memset(p, 0, sizeof(*p));
  p->hdc     = h;
  p->rcPaint = (RECT){0, 0, w->dc.window.w, w->dc.window.h};
  w->paint   = 0;
  return h;
}
BOOL WINAPI EndPaint(HWND h, const PAINTSTRUCT *p) {
  if (!p || p->hdc != h || !lookup(h))
    return FALSE;
  GaiaPresentDC(h);
  return TRUE;
}
static int shift_held;
static void events(void) {
  struct wm_event e;
  while (count < 60 && wm_poll_event(&e) > 0) {
    if ((e.type == WM_EV_KEY_DOWN || e.type == WM_EV_KEY_UP) &&
        (e.param == KEY_LEFTSHIFT || e.param == KEY_RIGHTSHIFT))
      shift_held = e.type == WM_EV_KEY_DOWN;
    if (e.type == WM_EV_KEY_DOWN || e.type == WM_EV_KEY_UP) {
      char ch = keymap_to_ascii(e.param, shift_held);
      if (ch)
        e.param = (unsigned char)ch;
    }
    for (unsigned i = 0; i < 32; i++) {
      struct win *w = windows + i;
      if (!w->handle || (e.window_handle && e.window_handle != w->dc.window.handle))
        continue;
      /* Heimdall identifies the target in window_handle. */
      if (e.type == WM_EV_QUIT) {
        PostMessageA(w->handle, WM_CLOSE, 0, 0);
      } else if (e.type == WM_EV_RESIZE) {
        {
          w->dc.window.surface_va = e.surface_va;
          w->dc.window.pitch      = e.pitch;
          w->dc.window.w          = e.w;
          w->dc.window.h          = e.h;
          PostMessageA(w->handle, WM_SIZE, 0, (LPARAM)((e.h << 16) | (e.w & 65535)));
          InvalidateRect(w->handle, 0, TRUE);
        }
      } else {
        UINT msg = e.type == WM_EV_KEY_DOWN     ? WM_KEYDOWN
                   : e.type == WM_EV_KEY_UP     ? WM_KEYUP
                   : e.type == WM_EV_MOUSE_MOVE ? WM_MOUSEMOVE
                   : e.type == WM_EV_MOUSE_DOWN ? WM_LBUTTONDOWN
                   : e.type == WM_EV_MOUSE_UP   ? WM_LBUTTONUP
                                                : 0;
        if (msg)
          PostMessageA(
              w->handle, msg, (WPARAM)e.param, (LPARAM)(((uint32_t)e.y << 16) | (e.x & 65535)));
      }
    }
  }
}
BOOL WINAPI PeekMessageA(MSG *out, HWND h, UINT min, UINT max, UINT flags) {
  if (!out || flags & ~PM_REMOVE || (h && !lookup(h))) {
    SetLastError(87);
    return FALSE;
  }
  events();
  for (unsigned i = 0; i < count; i++)
    if (queue[i].message == WM_QUIT ||
        ((!h || queue[i].hwnd == h) &&
         ((!min && !max) || (queue[i].message >= min && queue[i].message <= max)))) {
      *out = queue[i];
      if (flags & PM_REMOVE) {
        for (unsigned j = i + 1; j < count; j++)
          queue[j - 1] = queue[j];
        count--;
      }
      return TRUE;
    }
  return FALSE;
}
BOOL WINAPI PeekMessageW(MSG *o, HWND h, UINT a, UINT b, UINT f) {
  return PeekMessageA(o, h, a, b, f);
}
BOOL WINAPI GetMessageA(MSG *out, HWND h, UINT min, UINT max) {
  if (!out || (h && !lookup(h))) {
    SetLastError(87);
    return -1;
  }
  while (!PeekMessageA(out, h, min, max, PM_REMOVE))
    gaia_sleep(1000000);
  return out->message == WM_QUIT ? 0 : 1;
}
BOOL WINAPI GetMessageW(MSG *o, HWND h, UINT a, UINT b) { return GetMessageA(o, h, a, b); }
BOOL WINAPI TranslateMessage(const MSG *m) {
  if (m && m->message == WM_KEYDOWN && m->wParam >= 32 && m->wParam < 127) {
    PostMessageA(m->hwnd, WM_CHAR, m->wParam, m->lParam);
    return TRUE;
  }
  return FALSE;
}
LRESULT WINAPI DispatchMessageA(const MSG *m) {
  struct win *w = m ? lookup(m->hwnd) : 0;
  return w ? w->procedure(m->hwnd, m->message, m->wParam, m->lParam) : 0;
}
LRESULT WINAPI DispatchMessageW(const MSG *m) { return DispatchMessageA(m); }
LRESULT WINAPI DefWindowProcA(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  (void)wp;
  (void)lp;
  if (msg == WM_NCCREATE)
    return TRUE;
  if (msg == WM_CLOSE) {
    DestroyWindow(h);
    return 0;
  }
  if (msg == WM_PAINT) {
    PAINTSTRUCT p = {0};
    BeginPaint(h, &p);
    EndPaint(h, &p);
  }
  return 0;
}
LRESULT WINAPI DefWindowProcW(HWND h, UINT m, WPARAM w, LPARAM l) {
  return DefWindowProcA(h, m, w, l);
}
