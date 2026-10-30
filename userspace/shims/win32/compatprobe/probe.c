#include <shims/win32/windows.h>
int WINAPI DependencyValue(void);
static int value;
static int *volatile relocated_pointer=&value;
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,void *reserved){(void)instance;(void)reserved;if(reason==DLL_PROCESS_ATTACH){if(DependencyValue()!=11)return FALSE;*relocated_pointer=42;}return TRUE;}
int WINAPI ProbeValue(void){return *relocated_pointer;}
