# OSWiko

Un sistema operativo fatto in casa per il **Wiko Ridge 4G** (Snapdragon 410, 2 GB di RAM), con interfaccia touch in stile iOS, giochi e app di sistema. Leggerissimo: l'intero sistema occupa **circa 5 MB** e all'avvio usa una quarantina di MB di RAM.

![OSWiko nell'emulatore](docs/screenshots/panoramica.png)

![I giochi: Balzo e Volpe in Fuga](docs/screenshots/giochi.png)

Per ora gira nell'emulatore QEMU. Il porting sul telefono vero è la fase 2 (vedi [docs/ROADMAP.md](docs/ROADMAP.md)).

## Cosa c'è

| | |
|---|---|
| **Schermata di blocco** | ora, data in italiano, "scorri verso l'alto per aprire" |
| **Home** | griglia di icone, dock in vetro, sfondo sfumato, animazione di apertura delle app |
| **Gesture** | trascina dal bordo inferiore verso l'alto per tornare alla home |
| **Volpe in Fuga** | corsa infinita in 3D su 3 corsie nel bosco: swipe per cambiare corsia, saltare i tronchi, scivolare sotto le staccionate; gemme da raccogliere |
| **Balzo** | platform a un tasto: il cubo corre da solo, tocca per saltare spuntoni, blocchi e burroni; percorso fisso da imparare, record e tentativi |
| **Orologio** | quadrante analogico con lancetta dei secondi + ora digitale |
| **Calcolatrice** | stile iOS, funziona anche con la tastiera del PC |
| **Disegno** | tela, 8 colori, 3 pennelli |
| **Memory** | trova le 8 coppie di forme |
| **Bolle** | tocca le bolle che salgono (senza game over, pensato per i più piccoli) |
| **Torcia** | schermo a piena luce, tocca per cambiare colore |
| **Impostazioni** | info dispositivo, memoria, batteria, luminosità, riavvia/spegni |

## Com'è fatto

```
kernel Linux 6.12 (minimale, ~4 MB)
└── /init            nostro: monta i filesystem, avvia e sorveglia i processi
    ├── /bin/ui      nostro: interfaccia grafica + app (C, disegno diretto sul framebuffer)
    └── /bin/osh     nostro: shell di debug sulla console seriale
```

Il kernel e i driver sono Linux. Tutto quello che c'è sopra è scritto da zero in C, senza librerie grafiche: forme anti-aliasing, trasparenze, sfocatura, testo vettoriale (font [Inter](https://github.com/rsms/inter) con [stb_truetype](https://github.com/nothings/stb)). Lo schermo si ridisegna solo quando succede qualcosa, così la CPU resta ferma e la batteria dura di più.

```
userland/
  init.c         PID 1
  osh.c          shell di debug
  ui.c / ui.h    sistema: blocco, home, dock, gesture, gestione app
  gfx.c / gfx.h  grafica (forme, alpha, blur, immagini)
  text.c         testo vettoriale con cache dei glifi
  input.c        touchscreen / tasti / tastiera (evdev)
  sys.c          batteria, luminosità, memoria via /sys e /proc
  app_*.c        le app
kernel/qemu.config   configurazione del kernel per l'emulatore
scripts/             initramfs e script di avvio
```

## Provarlo su Windows (senza compilare)

1. Installa QEMU: dal cmd lancia `winget install SoftwareFreedomConservancy.QEMU`, oppure scaricalo da <https://www.qemu.org/download/#windows>.
2. Su GitHub apri la scheda **Actions** del repo, scegli l'ultima build verde e scarica l'artifact **oswiko-emulatore** (zip).
3. Estrai lo zip e fai doppio clic su `run-qemu.bat`.

Comandi nell'emulatore: il **mouse** fa da dito (trascina per gli swipe), **ESC** = tasto Home, **F12** = tasto Power, **Invio** sblocca. Nei giochi funzionano anche le **frecce** e la **barra spaziatrice**. Nella finestra del cmd c'è la shell `osh` (digita `help`).

## Compilarlo (Linux o WSL2 su Windows)

```bash
sudo apt install gcc-aarch64-linux-gnu qemu-system-arm flex bison bc libssl-dev libelf-dev
```
```bash
make
```
```bash
make run
```

La prima volta `make` scarica il sorgente del kernel (~250 MB) e lo compila (pochi minuti). Dopo, cambiare l'interfaccia richiede pochi secondi.

## Aggiungere una app

1. Crea `userland/app_nome.c` con una `struct app` (nome, colori dell'icona, funzioni `draw`/`event`). Guarda `app_torch.c`, è la più semplice.
2. Dichiarala in `ui.h` e aggiungila a `grid_apps` o a `dock_apps` in `ui.c`.
3. Aggiungi il file a `UI_SRC` in `userland/Makefile`.

## Licenze

Codice OSWiko: da definire. Font Inter: SIL OFL 1.1 (`assets/fonts/OFL.txt`). stb_truetype: pubblico dominio / MIT. Kernel Linux: GPL-2.0.
