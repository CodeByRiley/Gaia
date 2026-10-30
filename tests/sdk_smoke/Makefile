# A standalone Gaia application, as a third party would write it: no -I, no
# -L, no path back into the Gaia tree. Only the installed SDK.
CC ?= x86_64-unknown-gaia-gcc
ifeq ($(origin CC),default)
CC := x86_64-unknown-gaia-gcc
endif

# The toolchain's LINK_SPEC does not select Gaia's linker script yet, and
# binutils' default places the image at 0x400000, below where the Gaia loader
# accepts one (0x40000000). Drop this once LINK_SPEC carries
#   %{!T*:-T %:find-file(gaia.ld)}
LDSCRIPT := -T $(shell $(CC) -print-file-name=gaia.ld)

sdksmoke.elf: main.c
	$(CC) -O2 -Wall -Wextra -Werror $(LDSCRIPT) main.c -lgaia -o $@

.PHONY: clean
clean:
	rm -f sdksmoke.elf
