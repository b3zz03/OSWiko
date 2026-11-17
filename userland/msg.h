/* OSWiko - Messaggi: file condivisi tra il servizio msgd e l'app.
 *
 * msgd scrive i messaggi in MSG_LOG (una riga per messaggio) e lo stato in
 * MSG_STATE. L'app aggiunge i messaggi da inviare in MSG_OUTBOX. Entrambi
 * usano inotify per accorgersi delle modifiche, senza consumare CPU.
 *
 *   log:    id \t ora_unix \t I|O \t testo   (I = ricevuto, O = inviato)
 *   outbox: id \t testo
 *   state:  chiave=valore (status, code, name)
 *
 * Nel testo \n, \t e \\ sono scritti come sequenze di escape.
 */
#ifndef OSWIKO_MSG_H
#define OSWIKO_MSG_H

#define MSG_DIR      "/data/msg"
#define MSG_LOG      MSG_DIR "/log"
#define MSG_OUTBOX   MSG_DIR "/outbox"
#define MSG_RUN      "/run/msg"
#define MSG_STATE    MSG_RUN "/state"
#define MSG_CONF     "/data/telegram.conf"
#define MSG_FWCFG    "/sys/firmware/qemu_fw_cfg/by_name/opt/oswiko/"

/* Escape/unescape del testo per i file a righe. */
void msg_escape(const char *in, char *out, int n);
void msg_unescape(const char *in, char *out, int n);

#endif
