#include <shims/win32/windows.h>
static int value;
static int *volatile relocated_pointer=&value;
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,void *reserved){(void)instance;(void)reserved;if(reason==DLL_PROCESS_ATTACH)*relocated_pointer=11;return TRUE;}
int WINAPI DependencyValue(void){return *relocated_pointer;}
