@echo off
rem OSWiko - avvio nellemulatore QEMU su Windows
rem Metti questo file nella stessa cartella di Image e initramfs.cpio.gz
rem Mouse = dito, ESC = tasto Home, F12 = tasto Power
cd /d "%~dp0"
set QEMU=qemu-system-aarch64.exe
where %QEMU% >nul 2>nul || set QEMU="C:\Program Files\qemu\qemu-system-aarch64.exe"
%QEMU% -machine virt -cpu cortex-a53 -smp 4 -m 1024 -kernel Image -initrd initramfs.cpio.gz -append "console=ttyAMA0 quiet loglevel=4" -device virtio-gpu-pci,xres=720,yres=1280 -device virtio-tablet-pci -device virtio-keyboard-pci -serial mon:stdio -display gtk,zoom-to-fit=on
pause
