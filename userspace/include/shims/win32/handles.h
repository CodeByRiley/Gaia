#ifndef GAIA_WIN_HANDLES_H
#define GAIA_WIN_HANDLES_H
#include "windef.h"
enum gaia_handle_type { GH_FILE = 1, GH_THREAD = 2, GH_EVENT = 3 };
unsigned GaiaHandleType(HANDLE);
HANDLE GaiaHandleCreate(unsigned, int64_t, unsigned);
BOOL GaiaHandleLookup(HANDLE, unsigned, int64_t *, unsigned *);
BOOL GaiaHandleClose(HANDLE);
DWORD GaiaError(int64_t);
int GaiaPath(const char *, char *, size_t);
int GaiaUtf8(const WCHAR *, char *, size_t);
int GaiaUtf16(const char *, WCHAR *, size_t);
#endif
