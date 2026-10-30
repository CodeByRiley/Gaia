/* Ordinary Win32 SDK window/message-loop fixture. No Gaia dependencies. */
#include <windows.h>
static void report(const char *s) {
  DWORD n;
  const char *p = s;
  while (*p)
    p++;
  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)(p - s), &n, NULL);
}
static LRESULT CALLBACK procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_PAINT) {
    PAINTSTRUCT p;
    HDC dc = BeginPaint(h, &p);
    RECT r;
    GetClientRect(h, &r);
    HBRUSH b = CreateSolidBrush(RGB(35, 80, 145));
    FillRect(dc, &r, b);
    DeleteObject(b);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    TextOutA(dc, 24, 24, "Win32 on Heimdall", 17);
    Rectangle(dc, 16, 16, 220, 64);
    BITMAPINFO bitmap={0};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bitmap.bmiHeader.biWidth=2;bitmap.bmiHeader.biHeight=-2;bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;
    DWORD pixels[4]={0xff0000,0x00ff00,0x0000ff,0xffffff};
    SetDIBitsToDevice(dc,24,80,2,2,0,0,0,2,pixels,&bitmap,DIB_RGB_COLORS);
    EndPaint(h, &p);
    report("wingui: painted\n");
    return 0;
  }
  if (m == WM_CHAR) {
    report("wingui: keyboard input\n");
    if (w == 'q')
      DestroyWindow(h);
    return 0;
  }
  if (m == WM_LBUTTONDOWN) {
    report("wingui: mouse input\n");
    return 0;
  }
  if (m == WM_DESTROY) {
    report("wingui: destroyed\n");
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcA(h, m, w, l);
}
void mainCRTStartup(void) {
  WNDCLASSA c = {0};
  c.lpfnWndProc = procedure;
  c.hInstance = GetModuleHandleA(NULL);
  c.lpszClassName = "GaiaWin32Fixture";
  if (!RegisterClassA(&c))
    ExitProcess(1);
  HWND h = CreateWindowExA(0, c.lpszClassName, "Win32 compatibility",
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           400, 240, NULL, NULL, c.hInstance, NULL);
  if (!h)
    ExitProcess(2);
  ShowWindow(h, SW_SHOW);
  UpdateWindow(h);
  report("wingui: ready\n");
  MSG m;
  BOOL result;
  while ((result = GetMessageA(&m, NULL, 0, 0)) > 0) {
    TranslateMessage(&m);
    DispatchMessageA(&m);
  }
  ExitProcess(result < 0 ? 3 : (UINT)m.wParam);
}
