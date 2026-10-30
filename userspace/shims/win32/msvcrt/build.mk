LIB_NAME := msvcrt
LIB_TYPE := shared
LIB_FORMAT := pe
LIB_ABI := win32
LIB_SOURCES := runtime.c
LIB_DEPS := ntdll kernelbase
LIB_EXPORTS := msvcrt.def
