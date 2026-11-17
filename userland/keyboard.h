/* OSWiko - tastiera a schermo (italiano) */
#ifndef OSWIKO_KEYBOARD_H
#define OSWIKO_KEYBOARD_H

#include "input.h"

enum kb_action { KB_NONE, KB_TEXT, KB_BACKSPACE, KB_ENTER };

int  kb_height(void);
int  kb_top(void);                      /* y del bordo superiore */
void kb_draw(void);
/* Gestisce un tocco. Con KB_TEXT il testo (UTF-8) è in out. */
enum kb_action kb_event(const struct event *ev, char *out, int n);
void kb_set_shift(int on);              /* maiuscola automatica a inizio frase */
/* Tastiera fisica (emulatore): converte un tasto in testo o azione. */
enum kb_action kb_physical(const struct event *ev, char *out, int n);

#endif
