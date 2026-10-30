LIB_NAME := user32
LIB_TYPE := shared
LIB_FORMAT := pe
LIB_ABI := win32
LIB_SOURCES := window.c services.c ../../../lib/wm.c ../../../lib/event.c ../../../lib/keymap.c
LIB_DEPS := ntdll kernelbase
LIB_EXPORTS := user32.def
