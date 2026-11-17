# OSWiko - Roadmap

Obiettivo: un sistema operativo completo per il Wiko Ridge 4G, da dare a un bambino. Deve essere bello (stile Apple), semplice, sicuro e molto leggero.

## Fase 1 - Base nell'emulatore ✅

- [x] Kernel Linux minimale, init, shell di debug
- [x] Motore grafico: anti-aliasing, trasparenze, sfocatura, testo vettoriale
- [x] Blocco schermo, home, dock, gesture, animazioni
- [x] App: Orologio, Calcolatrice, Disegno, Memory, Bolle, Torcia, Impostazioni
- [x] Giochi: Balzo (platform a un tasto), Volpe in Fuga (corsa infinita 3D a 3 corsie)
- [x] Ottimizzazioni grafiche: cerchi a tratti, poligoni ritagliati allo schermo, terreno 3D a scanline
- [x] Messaggi via bot Telegram: servizio msgd (HTTPS/mbedTLS), abbinamento con codice, chat, tastiera a schermo, notifiche e badge
- [x] Memoria permanente nell'emulatore (disco ext4 data.img su /data): chat, collegamento, record, foto
- [x] Fotocamera (V4L2 o virtuale), Galleria, foto nella chat in entrambe le direzioni, Disegno salva in Galleria
- [x] Build automatica su GitHub e avvio su Windows

## Fase 2 - Sul telefono vero

Serve un **cavo micro-USB dati** (quelli che fanno solo ricarica non vanno bene).

- [ ] Entrare in fastboot o in EDL (Qualcomm 9008) e salvare un backup completo della memoria
- [ ] Sbloccare il bootloader
- [ ] Installare [lk2nd](https://github.com/msm8916-mainline/lk2nd), il bootloader secondario open source per MSM8916
- [ ] Kernel [msm8916-mainline](https://github.com/msm8916-mainline/linux) con un device tree per il Ridge 4G, ricavato dal kernel Android originale e da telefoni simili già supportati
- [ ] `kernel/wiko-ridge.config`, come `qemu.config` ma per il telefono
- [ ] Verifica display, touch, tasti (Power, Volume), batteria, retroilluminazione
- [ ] Partizione `userdata` come `/data` (ext4): stesso meccanismo del disco dell'emulatore (`oswiko.data=` sulla riga di comando del kernel)
- [ ] Fotocamera: serve un driver per il sensore del Ridge 4G (il chip, CAMSS, è supportato; il sensore va verificato). L'app usa già V4L2
- [ ] Sospensione: con lo schermo spento il telefono deve consumare quasi niente

Il codice dell'interfaccia è già pronto per il telefono: usa `/dev/fb0`, il touchscreen multitouch, i tasti Power e Volume e la retroilluminazione da `/sys/class/backlight`.

## Fase 3 - Sistema di app e Store

- [ ] **App come processi separati.** Ogni app disegna nel proprio buffer in memoria condivisa e il sistema (compositor) compone lo schermo. Se un gioco va in crash, il resto continua a funzionare.
- [ ] **Formato pacchetto `.owk`**: archivio con manifest (nome, versione, icona, permessi) + programma
- [ ] **OSWiko Store**: catalogo dei giochi in un repo GitHub, scarica e installa via Wi-Fi
- [ ] **Sandbox**: ogni app vede solo i propri file (namespaces + seccomp di Linux)
- [ ] **SDK** per scrivere giochi: la stessa API di `gfx.h` / `input.h`, più audio
- [ ] **Supporto SDL2**: permette di portare molti giochi open source già esistenti

## Fase 4 - Funzioni da sistema moderno

- [ ] Wi-Fi (driver `wcn36xx`, già nel kernel mainline) + schermata di connessione
- [ ] Centro di controllo (scorri dall'alto): luminosità, Wi-Fi, volume, torcia
- [ ] Notifiche
- [ ] Audio (codec MSM8916 supportato nel mainline)
- [ ] Tastiera a schermo
- [ ] Più pagine nella home, cartelle, sfondi a scelta
- [ ] **Controllo genitori**: limite di tempo giornaliero, PIN per Impostazioni e Store
- [ ] Aggiornamenti del sistema via Wi-Fi
- [ ] Disegno diretto nei buffer DRM con doppio buffer (page flip): elimina la copia di ogni fotogramma
- [ ] Ottimizzazioni NEON per riempimenti e fusioni
- [ ] Salvataggio dei record dei giochi su /data
- [ ] Messaggi: emoji (font monocromatico), messaggi vocali, più contatti
- [ ] Accelerazione GPU (Adreno 306, driver freedreno) se servirà; per ora la CPU basta

## E il Play Store?

In breve: su un sistema fatto da noi **non si può avere il Play Store in modo pratico.** I motivi:

1. **Le app Android hanno bisogno di Android.** Un'app Android non è un programma Linux normale: usa Java/ART, i servizi di sistema Android, Binder e la sua grafica. Per farle girare bisogna far girare Android stesso.
2. **Waydroid** (Android dentro un container Linux) esiste e in teoria funziona su MSM8916. Ma vuol dire far girare Android intero *in più* rispetto al nostro sistema, su 2 GB di RAM e senza accelerazione GPU affidabile: lento e con poca memoria libera. Il contrario di "efficiente".
3. **Il Play Store vero** richiede la certificazione di Google. Sui sistemi non certificati funziona male o va registrato a mano, e molte app (soprattutto i giochi) si rifiutano di partire.

Se il Play Store è un requisito irrinunciabile, la strada giusta è un'altra: installare una ROM Android leggera (es. LineageOS) e creare *sopra* un launcher OSWiko in stile Apple. Così però il sistema operativo non è più nostro.

**La proposta per OSWiko** è un nostro Store (fase 3) con:
- giochi scritti per OSWiko, piccoli e veloci;
- giochi open source portati tramite SDL2;
- tutto controllato da te: niente pubblicità, niente acquisti in-app, niente contenuti non adatti. Per un bambino è un vantaggio, non un limite.

Waydroid si può comunque provare più avanti come esperimento, quando il resto funziona sul telefono.

## Budget di memoria (obiettivo)

| | RAM |
|---|---|
| Kernel | ~15 MB |
| Sistema (init + ui) | ~25 MB (schermo 720×1280 + sfondo + cache) |
| Ogni app aperta | 4–20 MB |
| **Libera per i giochi** | **~1,8 GB** |

Per confronto, Android sullo stesso telefono occupa diverse centinaia di MB solo per il sistema.
