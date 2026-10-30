# Registry keys are format.runtime. Library names never become raw linker inputs.
RUNTIME_elf.musl_CFLAGS = $(MUSL_CFLAGS)
RUNTIME_elf.musl_DEPS = $(MUSL_LIBC) $(MUSL_LINK) $(MUSL_LINKER_SCRIPT)
RUNTIME_elf.musl_LIBS := gaia
RUNTIME_elf.musl_gaia = $(LIBGAIA)
RUNTIME_elf.native_CFLAGS = $(USER_CFLAGS)
RUNTIME_elf.native_DEPS = lib/user.ld
RUNTIME_elf.native_LIBS := gaia
RUNTIME_elf.native_gaia = $(LIB_OBJS)
RUNTIME_pe.native_CFLAGS = $(PE_CFLAGS)
RUNTIME_pe.native_LIBS := gaia
RUNTIME_pe.native_gaia = $(PE_LIB_OBJS)
SUPPORTED_RUNTIMES := elf.musl elf.native pe.native

RUNTIME_pe.win32_CFLAGS = -ffreestanding -nostdlib -fno-pie -mno-red-zone -static -fno-asynchronous-unwind-tables -fno-stack-protector -O2 -Wall -Wextra -std=gnu23
RUNTIME_pe.win32_LIBS := kernel32 user32 gdi32 msvcrt
RUNTIME_pe.win32_kernel32 = $(shell $(USER_PE_CC) -print-file-name=libkernel32.a)
RUNTIME_pe.win32_user32 = $(shell $(USER_PE_CC) -print-file-name=libuser32.a)
RUNTIME_pe.win32_gdi32 = $(shell $(USER_PE_CC) -print-file-name=libgdi32.a)
RUNTIME_pe.win32_msvcrt = $(shell $(USER_PE_CC) -print-file-name=libmsvcrt.a)
SUPPORTED_RUNTIMES += pe.win32

RUNTIME_elf.musl_LIBS += posix_shared linux_compat
RUNTIME_elf.native_LIBS += posix_shared linux_compat
RUNTIME_elf.musl_posix_shared = ../build/userspace/elf/shims/posix_shared.a
RUNTIME_elf.native_posix_shared = ../build/userspace/elf/shims/posix_shared.a
RUNTIME_elf.musl_linux_compat = ../build/userspace/elf/shims/linux_compat.a ../build/userspace/elf/shims/posix_shared.a
RUNTIME_elf.native_linux_compat = ../build/userspace/elf/shims/linux_compat.a ../build/userspace/elf/shims/posix_shared.a
