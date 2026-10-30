APP_FORMAT := elf
APP_RUNTIME := musl

APP_SOURCES := \
    sh.c \
    console.c \
    input.c \
    line.c \
    complete.c \
    parse.c \
    path.c \
    builtin.c \
    exec.c
