# libgaia SDK: the public <gaia/...> headers and a libgaia.a built by the
# Gaia GCC toolchain (x86_64-unknown-gaia-gcc), staged under
# ../build/userspace/sdk and installed into that toolchain's sysroot.
#
#   make sdk            build and stage
#   make sdk-check      header, symbol, and link checks against the staged SDK
#   make sdk-install    check, then install into $(GAIA_SYSROOT)
#   make sdk-uninstall  remove exactly what the last sdk-install wrote
#
# The Gaia toolchain lives wherever it was built (WSL, for this tree), so run
# these from a shell that has it on PATH. They need nasm but none of the
# x86_64-elf or mingw tools, and nothing here is part of `all`.
#
# The sources are LIBGAIA_SRCS, the same list the in-tree musl libgaia.a is
# built from, compiled again rather than copied: those objects come from
# x86_64-elf-gcc against the source-tree musl headers, these from the SDK
# compiler against the sysroot it will be linked in.

SDK_TARGET   ?= x86_64-unknown-gaia
SDK_CC       ?= $(SDK_TARGET)-gcc
SDK_AR       ?= $(SDK_TARGET)-ar
SDK_NM       ?= $(SDK_TARGET)-nm
# Recursive on purpose: only an sdk target pays for asking the compiler.
GAIA_SYSROOT ?= $(shell $(SDK_CC) -print-sysroot)

SDK_DIR     := ../build/userspace/sdk
SDK_OBJ_DIR := $(SDK_DIR)/obj
SDK_LIBGAIA := $(SDK_DIR)/lib/libgaia.a

# The compiler's sysroot (musl) is searched first; the source tree only after
# it, through -idirafter. With -I, userspace/include's own stdio.h and
# friends would shadow musl's , the same reason MUSL_CFLAGS lists the musl
# directories ahead of Gaia's.
SDK_CFLAGS := -O2 -Wall -Wextra -std=gnu11 -fno-pie -mno-red-zone \
              -DGAIA_USE_MUSL \
              -idirafter . -idirafter lib -idirafter include -idirafter ../kernel

SDK_OBJS := $(patsubst lib/%.c,$(SDK_OBJ_DIR)/%.o,$(LIBGAIA_SRCS)) $(SDK_OBJ_DIR)/syscall_asm.o

# Public headers are everything directly under include/gaia. bits/ is staged
# separately: in the tree its syscall_abi.h forwards to the kernel's, and the
# SDK carries a verbatim copy of the kernel's instead.
SDK_PUBLIC_HEADERS := $(wildcard include/gaia/*.h)
SDK_STAGED_PUBLIC  := $(patsubst include/%,$(SDK_DIR)/include/%,$(SDK_PUBLIC_HEADERS))
SDK_STAGED_ABI     := $(SDK_DIR)/include/gaia/bits/syscall_abi.h
SDK_STAGED_HEADERS := $(SDK_STAGED_PUBLIC) $(SDK_STAGED_ABI)

.PHONY: sdk sdk-check sdk-install sdk-uninstall
sdk: $(SDK_LIBGAIA) $(SDK_STAGED_HEADERS)

$(filter-out %/syscall_asm.o,$(SDK_OBJS)): $(SDK_OBJ_DIR)/%.o: lib/%.c build/sdk.mk
	@mkdir -p $(dir $@)
	$(SDK_CC) $(SDK_CFLAGS) -MMD -MP -c $< -o $@

$(SDK_OBJ_DIR)/syscall_asm.o: lib/syscall.s
	@mkdir -p $(dir $@)
	$(USER_AS) -f elf64 $< -o $@

$(SDK_LIBGAIA): $(SDK_OBJS)
	@mkdir -p $(dir $@)
	rm -f $@ && $(SDK_AR) rcs $@ $^

$(SDK_STAGED_PUBLIC): $(SDK_DIR)/include/%: include/%
	@mkdir -p $(dir $@)
	cp $< $@

$(SDK_STAGED_ABI): ../kernel/arch/syscall_abi.h
	@mkdir -p $(dir $@)
	cp $< $@

# The drift check compiles the in-tree headers and the public ones into one
# translation unit, so it needs the in-tree include paths as well.
SDK_SCRIPT_ENV = SDK_CC="$(SDK_CC)" SDK_NM="$(SDK_NM)" SDK_DIR="$(SDK_DIR)" \
                 SDK_INTREE_CFLAGS="$(SDK_CFLAGS)" SDK_LDSCRIPT="$(MUSL_LINKER_SCRIPT)" \
                 GAIA_SYSROOT="$(GAIA_SYSROOT)"

# bash, not $(SHELL): the script uses arrays and pipefail, and make's shell
# is dash on Debian-family systems.
sdk-check: sdk
	$(SDK_SCRIPT_ENV) bash tools/sdk.sh check

sdk-install: sdk-check
	$(SDK_SCRIPT_ENV) bash tools/sdk.sh install

sdk-uninstall:
	$(SDK_SCRIPT_ENV) bash tools/sdk.sh uninstall

-include $(SDK_OBJS:.o=.d)
