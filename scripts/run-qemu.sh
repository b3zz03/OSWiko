#!/bin/sh
# OSWiko - avvia nell'emulatore QEMU
#   ./scripts/run-qemu.sh            finestra grafica 360x640 (WIDTH=720 HEIGHT=1280 per la risoluzione del Wiko)
#   ./scripts/run-qemu.sh --serial   senza finestra, solo console
# Nella finestra: il mouse fa da dito, ESC = tasto Home, F12 = tasto Power.
# Trascina dal bordo in basso verso l'alto per tornare alla home.
set -e
cd "$(dirname "$0")/.."

WIDTH=${WIDTH:-360}
HEIGHT=${HEIGHT:-640}
DISPLAY_OPT="-display gtk,zoom-to-fit=off"
[ "$1" = "--serial" ] && DISPLAY_OPT="-display none"

# Memoria permanente: data.img è il "disco" del telefono (chat, foto, record).
# La prima volta viene creato copiando il disco vuoto.
[ -f data.img ] || cp out/data-vuoto.img data.img

# Messaggi: se c'è telegram.conf (token del bot) lo passa al sistema
CONF_OPT=""
[ -f telegram.conf ] && CONF_OPT="-fw_cfg name=opt/oswiko/telegram.conf,file=telegram.conf"

exec qemu-system-aarch64 \
	-machine virt -cpu cortex-a53 -smp 4 -m 1024 \
	-kernel out/Image -initrd out/initramfs.cpio.gz \
	-append "console=ttyAMA0 quiet loglevel=4 ip=dhcp oswiko.data=/dev/vda" \
	-device virtio-gpu-pci,xres=$WIDTH,yres=$HEIGHT \
	-device virtio-tablet-pci -device virtio-keyboard-pci \
	-drive file=data.img,format=raw,if=virtio \
	-netdev user,id=n0 -device virtio-net-pci,netdev=n0 $CONF_OPT \
	-serial mon:stdio $DISPLAY_OPT
