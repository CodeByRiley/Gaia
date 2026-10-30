LIB_NAME := gdi32
LIB_TYPE := shared
LIB_FORMAT := pe
LIB_ABI := win32
LIB_SOURCES := drawing.c
LIB_DEPS := user32 ntdll
LIB_EXPORTS := gdi32.def
