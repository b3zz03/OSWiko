# OSWiko - programmi utente (compilati statici per ARM64)
CROSS_COMPILE ?= aarch64-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
STRIP   := $(CROSS_COMPILE)strip
OUT     ?= ../build/userland
CFLAGS  ?= -Os -pipe
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
           -ffunction-sections -fdata-sections
LDFLAGS += -static -Wl,--gc-sections

UI_SRC := ui.c gfx.c text.c input.c sys.c \
          app_clock.c app_calc.c app_paint.c app_memory.c app_bubbles.c \
          app_torch.c app_settings.c

all: $(OUT)/init $(OUT)/ui $(OUT)/osh

$(OUT):
	mkdir -p $@

$(OUT)/init: init.c | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	$(STRIP) $@

$(OUT)/osh: osh.c | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	$(STRIP) $@

$(OUT)/ui: $(UI_SRC) gfx.h ui.h input.h | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(UI_SRC) -lm
	$(STRIP) $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
