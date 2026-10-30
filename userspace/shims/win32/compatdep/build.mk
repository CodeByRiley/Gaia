LIB_NAME := compatdep
LIB_TYPE := shared
LIB_FORMAT := pe
LIB_ABI := win32
LIB_SOURCES := probe.c
LIB_ENTRY := DllMain
LIB_EXPORTS := compatdep.def
LIB_LDFLAGS := -Wl,--image-base,0x140000000
