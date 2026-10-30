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
