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
          app_torch.c app_settings.c app_jump.c app_runner.c \
          app_chat.c keyboard.c msgcommon.c \
          app_gallery.c app_camera.c camera_v4l2.c photos.c image.c

MBEDTLS ?= ../build/mbedtls
TLS_CFLAGS := -I$(MBEDTLS)/include
TLS_LIBS   := $(MBEDTLS)/library/libmbedtls.a $(MBEDTLS)/library/libmbedx509.a \
              $(MBEDTLS)/library/libmbedcrypto.a

all: $(OUT)/init $(OUT)/ui $(OUT)/osh $(OUT)/msgd

$(OUT):
	mkdir -p $@

$(OUT)/init: init.c | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	$(STRIP) $@

$(OUT)/osh: osh.c | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	$(STRIP) $@

$(OUT)/msgd: msgd.c net.c msgcommon.c msg.h net.h | $(OUT)
	$(CC) $(CFLAGS) $(TLS_CFLAGS) $(LDFLAGS) -o $@ msgd.c net.c msgcommon.c $(TLS_LIBS) -lpthread
	$(STRIP) $@

$(OUT)/ui: $(UI_SRC) gfx.h ui.h input.h keyboard.h msg.h photos.h | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(UI_SRC) -lm
	$(STRIP) $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
