APP_FORMAT := elf
APP_NAME := themis
APP_RUNTIME := musl

APP_SOURCES := themis.c state.c window.c input.c render.c settings.c pages.c \
               pages/system.c pages/display.c pages/appearance.c \
               pages/audio.c pages/about.c

APP_LIBS := gaia
