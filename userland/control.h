/* OSWiko - Centro di controllo */
#ifndef OSWIKO_CONTROL_H
#define OSWIKO_CONTROL_H

#include "ui.h"

int  cc_active(void);
void cc_begin_pull(int y);       /* dito sul bordo superiore */
void cc_close(void);
void cc_draw(void);
void cc_event(const struct event *ev);
int  cc_next_ms(void);           /* ms al prossimo cc_tick, -1 = nessuno */
void cc_tick(void);

/* icone usate anche dalla barra di stato */
void cc_icon_airplane(float cx, float cy, float s, color_t c);
void cc_icon_wifi(float cx, float cy, float s, color_t c);
void cc_icon_moon(float cx, float cy, float s, color_t c, color_t bg);
void cc_icon_speaker(float cx, float cy, float s, color_t c, int level);

#endif
