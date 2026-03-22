BOARD_CC ?= arm-linux-gcc
PC_CC ?= gcc

COMMON_CFLAGS = -I. -I./lvgl -O3 -std=gnu99
LVGL_SRCS = $(shell find lvgl/src -name "*.c")
COMMON_SRCS = main.c my_font_full.c $(LVGL_SRCS)
BOARD_SRCS = $(COMMON_SRCS) platform_board.c
PC_SRCS = $(COMMON_SRCS) platform_pc.c

BOARD_CFLAGS = $(COMMON_CFLAGS) -DLV_CONF_INCLUDE_SIMPLE
BOARD_LDFLAGS = -lm -lpthread
BOARD_TARGET = piano_app

PC_SDL_CFLAGS ?= $(shell pkg-config --cflags sdl2 2>/dev/null)
PC_SDL_LIBS ?= $(shell pkg-config --libs sdl2 2>/dev/null)
PC_CFLAGS = $(COMMON_CFLAGS) $(PC_SDL_CFLAGS) -DLV_CONF_PATH=lv_conf_pc.h
PC_LDFLAGS = $(PC_SDL_LIBS) -lm -lpthread
PC_TARGET = piano_app_sim

.PHONY: all board pc clean

all: board

board: $(BOARD_TARGET)

pc: $(PC_TARGET)

$(BOARD_TARGET): $(BOARD_SRCS) platform.h
	$(BOARD_CC) $(BOARD_CFLAGS) -o $@ $(BOARD_SRCS) $(BOARD_LDFLAGS)

$(PC_TARGET): $(PC_SRCS) lv_conf_pc.h platform.h
	$(PC_CC) $(PC_CFLAGS) -o $@ $(PC_SRCS) $(PC_LDFLAGS)

clean:
	rm -f $(BOARD_TARGET) $(PC_TARGET) *.o
