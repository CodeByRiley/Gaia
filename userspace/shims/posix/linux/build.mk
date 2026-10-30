LIB_NAME := linux_compat
LIB_FORMAT := elf
LIB_ABI := posix
LIB_SOURCES := syscall.c errno.c futex.c
LIB_DEPS := posix_shared
