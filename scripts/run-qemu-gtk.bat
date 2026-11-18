@echo off
rem OSWiko - alternativa con finestra GTK (su alcuni Windows non mostra l'immagine)
rem Metti questo file nella stessa cartella di Image e initramfs.cpio.gz
rem Mouse = dito, ESC = tasto Home, F12 = tasto Power
rem Per i Messaggi: metti telegram.conf (con il token del bot) nella stessa cartella
rem data.img e la memoria del telefono (chat, foto, record): NON cancellarla
rem Puoi cambiare la dimensione dello schermo qui sotto (larghezza x altezza, 9:16)
set W=360
set H=640
cd /d "%~dp0"
set QEMU=qemu-system-aarch64.exe
where %QEMU% >nul 2>nul || set QEMU="C:\Program Files\qemu\qemu-system-aarch64.exe"
if not exist data.img copy data-vuoto.img data.img >nul
set CONF=
if exist telegram.conf set CONF=-fw_cfg name=opt/oswiko/telegram.conf,file=telegram.conf
%QEMU% -machine virt -cpu cortex-a53 -smp 4 -m 1024 -kernel Image -initrd initramfs.cpio.gz -append "console=ttyAMA0 quiet loglevel=4 ip=dhcp oswiko.data=/dev/vda" -device virtio-gpu-pci,xres=%W%,yres=%H% -device virtio-tablet-pci -device virtio-keyboard-pci -drive file=data.img,format=raw,if=virtio -netdev user,id=n0 -device virtio-net-pci,netdev=n0 %CONF% -serial mon:stdio -display gtk,zoom-to-fit=off
pause
