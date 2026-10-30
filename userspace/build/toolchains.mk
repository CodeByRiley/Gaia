USER_ELF_CC  := x86_64-elf-gcc
USER_ELF_CXX := x86_64-elf-g++
USER_ELF_LD  := x86_64-elf-ld

#USER_ELF_CC  := /c/elf-tools/bin/x86_64-elf-gcc
#USER_ELF_LD  := /c/elf-tools/bin/x86_64-elf-ld

USER_PE_CC   := x86_64-w64-mingw32-gcc
USER_AS      := nasm
USER_CFLAGS  := -ffreestanding -nostdlib -fno-pie -mno-red-zone -static \
                  -I . -I lib -I include -I ../kernel -O2 -Wall -Wextra -std=gnu23 \
                  -MMD -MP -DDEBUG
USER_LDFLAGS := -nostdlib -static -T lib/user.ld

PE_LDFLAGS := -nostdlib -static -e _start \
              -Wl,--image-base,0x140000000 -Wl,--subsystem,console

PE_CFLAGS := -ffreestanding -nostdlib -fno-pie -mno-red-zone -static \
							-fno-asynchronous-unwind-tables \
              -I . -I lib -I include -I ../kernel -O2 -Wall -Wextra -std=gnu23 -DDEBUG
