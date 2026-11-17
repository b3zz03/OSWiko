#!/bin/sh
# OSWiko - avvia nell'emulatore QEMU
#   ./scripts/run-qemu.sh            finestra grafica 720x1280 (come il Wiko Ridge 4G)
#   ./scripts/run-qemu.sh --serial   senza finestra, solo console
# Nella finestra: il mouse fa da dito, ESC = tasto Home, F12 = tasto Power.
# Trascina dal bordo in basso verso l'alto per tornare alla home.
set -e
cd "$(dirname "$0")/.."

WIDTH=${WIDTH:-720}
HEIGHT=${HEIGHT:-1280}
DISPLAY_OPT="-display gtk,zoom-to-fit=on"
[ "$1" = "--serial" ] && DISPLAY_OPT="-display none"

# Messaggi: se c'è telegram.conf (token del bot) lo passa al sistema
CONF_OPT=""
[ -f telegram.conf ] && CONF_OPT="-fw_cfg name=opt/oswiko/telegram.conf,file=telegram.conf"

exec qemu-system-aarch64 \
	-machine virt -cpu cortex-a53 -smp 4 -m 1024 \
	-kernel out/Image -initrd out/initramfs.cpio.gz \
	-append "console=ttyAMA0 quiet loglevel=4 ip=dhcp" \
	-device virtio-gpu-pci,xres=$WIDTH,yres=$HEIGHT \
	-device virtio-tablet-pci -device virtio-keyboard-pci \
	-netdev user,id=n0 -device virtio-net-pci,netdev=n0 $CONF_OPT \
	-serial mon:stdio $DISPLAY_OPT
