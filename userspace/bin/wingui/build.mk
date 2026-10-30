APP_FORMAT := pe
APP_RUNTIME := win32
APP_SOURCES := wingui.c
APP_LIBS := kernel32 user32 gdi32
APP_LDFLAGS := -Wl,--subsystem,windows
