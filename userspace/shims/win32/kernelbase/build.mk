LIB_NAME := kernelbase
LIB_TYPE := shared
LIB_FORMAT := pe
LIB_ABI := win32
LIB_SOURCES := file.c memory.c process.c
LIB_DEPS := ntdll
LIB_EXPORTS := kernelbase.def
