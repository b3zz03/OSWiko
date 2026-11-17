/* OSWiko - archivio foto in /data/photos (con miniature in .thumbs) */
#ifndef OSWIKO_PHOTOS_H
#define OSWIKO_PHOTOS_H

#include "gfx.h"

#define PHOTO_DIR "/data/photos"
#define THUMB_DIR PHOTO_DIR "/.thumbs"
#define PHOTO_MAX 300
#define THUMB_SIZE 200

/* Nomi dei file .jpg, dal più recente. Ritorna quanti. */
int  photos_list(char (*names)[64], int max);
/* Salva come IMG_AAAAMMGG_HHMMSS.jpg; il percorso finisce in out. */
int  photo_save(const struct image *im, char *out, int n);
/* Miniatura quadrata (la crea e la salva se non esiste). */
int  photo_thumb(const char *name, struct image *out);
void photo_delete(const char *name);
void photo_path(const char *name, char *out, int n);

/* Integrazioni tra app */
void gallery_show(const char *path);       /* apre la Galleria su una foto */
int  chat_can_send(void);                  /* Messaggi è collegato? */
const char *chat_contact(void);            /* nome di chi riceve */
void chat_send_photo(const char *path);    /* mette in coda una foto */

#endif
