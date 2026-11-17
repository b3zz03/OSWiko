#!/bin/sh
# OSWiko - emulatore collegato al finto server Telegram (vedi mock-telegram.sh)
cd "$(dirname "$0")/.."
exec qemu-system-aarch64 -machine virt -cpu cortex-a53 -smp 4 -m 1024 \
	-kernel out/Image -initrd out/initramfs.cpio.gz \
	-append "console=ttyAMA0 loglevel=4 ip=dhcp" \
	-device virtio-gpu-pci,xres=720,yres=1280 \
	-device virtio-tablet-pci -device virtio-keyboard-pci \
	-netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
	-fw_cfg name=opt/oswiko/telegram.conf,file=build/mock/telegram.conf \
	-fw_cfg name=opt/oswiko/test-ca.pem,file=build/mock/ca.pem \
	-serial mon:stdio -display gtk,zoom-to-fit=on
