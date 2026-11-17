# OSWiko - build completa
#
#   make            kernel + userland + initramfs
#   make run        avvia nell'emulatore QEMU (finestra grafica)
#   make run-serial avvia senza finestra (solo console seriale)
#   make clean      cancella build/ (tranne il sorgente del kernel)

ARCH          := arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
KERNEL_REPO   ?= https://github.com/gregkh/linux
KERNEL_TAG    ?= v6.12.118
JOBS          ?= $(shell nproc)
MBEDTLS_VER   ?= 3.6.8
MBEDTLS_URL   ?= https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$(MBEDTLS_VER)/mbedtls-$(MBEDTLS_VER).tar.bz2

BUILD   := build
KSRC    := $(BUILD)/linux
OUT     := out
KCONFIG := kernel/qemu.config

USERLAND_BINS := $(BUILD)/userland/init $(BUILD)/userland/ui $(BUILD)/userland/osh $(BUILD)/userland/msgd
FONTS := $(wildcard assets/fonts/*.otf)

all: $(OUT)/Image $(OUT)/initramfs.cpio.gz $(OUT)/data-vuoto.img

# ---- kernel ----
$(KSRC)/Makefile:
	git clone --depth 1 --branch $(KERNEL_TAG) $(KERNEL_REPO) $(KSRC)

$(KSRC)/.config: $(KCONFIG) | $(KSRC)/Makefile
	$(MAKE) -C $(KSRC) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
		allnoconfig KCONFIG_ALLCONFIG=$(abspath $(KCONFIG))

$(OUT)/Image: $(KSRC)/.config
	$(MAKE) -C $(KSRC) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -j$(JOBS) Image
	mkdir -p $(OUT)
	cp $(KSRC)/arch/arm64/boot/Image $@

kernel: $(OUT)/Image
kernel-menuconfig: $(KSRC)/.config
	$(MAKE) -C $(KSRC) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) menuconfig

# ---- mbedTLS (connessioni HTTPS per i messaggi) ----
MBEDTLS := $(BUILD)/mbedtls
$(MBEDTLS)/library/libmbedtls.a:
	mkdir -p $(BUILD)
	curl -sSfL -o $(BUILD)/mbedtls.tar.bz2 $(MBEDTLS_URL)
	rm -rf $(MBEDTLS) && mkdir -p $(MBEDTLS)
	tar xjf $(BUILD)/mbedtls.tar.bz2 -C $(MBEDTLS) --strip-components=1
	$(MAKE) -C $(MBEDTLS) lib CC=$(CROSS_COMPILE)gcc AR=$(CROSS_COMPILE)ar \
		CFLAGS="-Os -ffunction-sections -fdata-sections"

# ---- userland ----
userland: $(MBEDTLS)/library/libmbedtls.a
	$(MAKE) -C userland CROSS_COMPILE=$(CROSS_COMPILE) MBEDTLS=$(abspath $(MBEDTLS))

$(USERLAND_BINS): userland

# ---- initramfs (senza bisogno di root: usa gen_init_cpio del kernel) ----
$(BUILD)/gen_init_cpio: | $(KSRC)/Makefile
	mkdir -p $(BUILD)
	cc -O2 -o $@ $(KSRC)/usr/gen_init_cpio.c

$(OUT)/initramfs.cpio.gz: userland $(BUILD)/gen_init_cpio scripts/initramfs.list $(FONTS) assets/certs/cacert.pem
	mkdir -p $(OUT)
	$(BUILD)/gen_init_cpio scripts/initramfs.list > $(BUILD)/initramfs.cpio
	gzip -9 -c $(BUILD)/initramfs.cpio > $@

initramfs: $(OUT)/initramfs.cpio.gz

# ---- disco dati vuoto (ext4) per l'emulatore: diventa /data ----
$(OUT)/data-vuoto.img:
	mkdir -p $(OUT)
	rm -f $@
	truncate -s 256M $@
	mkfs.ext4 -q -F -L oswiko-data $@

# ---- emulatore ----
run: all
	./scripts/run-qemu.sh

run-serial: all
	./scripts/run-qemu.sh --serial

clean:
	$(MAKE) -C userland clean
	rm -rf $(OUT) $(BUILD)/initramfs.cpio $(BUILD)/gen_init_cpio

.PHONY: all kernel kernel-menuconfig userland initramfs run run-serial clean
