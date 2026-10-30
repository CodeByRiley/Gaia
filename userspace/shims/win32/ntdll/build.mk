LIB_NAME := ntdll
LIB_TYPE := shared
LIB_FORMAT := pe
LIB_ABI := win32
LIB_SOURCES := runtime.c thread.c sync.c startup.S
LIB_DEPS := win32_shared
LIB_EXPORTS := ntdll.def
