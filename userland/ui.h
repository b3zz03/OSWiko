/* OSWiko - interfaccia di sistema (SpringBoard) e API per le app */
#ifndef OSWIKO_UI_H
#define OSWIKO_UI_H

#include "gfx.h"
#include "input.h"

#define OSWIKO_VERSION "0.2"

/* Una app di sistema. Ogni app vive in app_*.c e si registra in apps[]. */
struct app {
	const char *name;
	color_t icon_top, icon_bottom;          /* gradiente dell'icona */
	void (*icon)(int x, int y, int size);   /* disegna il simbolo sull'icona */
	void (*open)(void);                     /* chiamata all'apertura */
	void (*draw)(void);                     /* disegna tutto lo schermo */
	void (*event)(const struct event *ev);  /* tocco o tasto */
	void (*timer)(void);                    /* scadenza di ui_timer() */
	void (*close)(void);                    /* opzionale: all'uscita */
	void (*init)(void);                     /* opzionale: all'avvio del sistema */
	int light;                              /* sfondo chiaro: barra di stato scura */
	int badge;                              /* numero sul pallino rosso (0 = niente) */
};

extern struct app app_clock, app_calc, app_paint, app_memory, app_bubbles,
	app_torch, app_settings, app_jump, app_runner, app_chat, app_gallery, app_camera;

/* Servizi per le app */
void ui_redraw(void);              /* chiede di ridisegnare lo schermo */
void ui_timer(int ms);             /* chiama app->timer() tra ms (0 = annulla) */
void ui_go_home(void);
void ui_open(struct app *a);       /* apre un'app (anche da un'altra app) */
void ui_power(int reboot);         /* spegne (0) o riavvia (1) */
long long ui_now_ms(void);
int  ui_status_h(void);            /* altezza barra di stato in pixel */
int  ui_is_open(struct app *a);    /* l'app è in primo piano? */
void ui_watch_fd(int fd, void (*cb)(int fd)); /* chiama cb quando fd è leggibile */
void ui_notify(struct app *a, const char *title, const char *text); /* banner */
void ui_badges_changed(void);

/* Impostazioni di sistema (centro di controllo) */
int  ui_brightness(void);  void ui_set_brightness(int v);   /* 0..100 */
int  ui_volume(void);      void ui_set_volume(int v);       /* 0..100 */
int  ui_dnd(void);         void ui_set_dnd(int on);         /* Non disturbare */
int  ui_airplane(void);    void ui_set_airplane(int on);
int  ui_wifi(void);        void ui_set_wifi(int on);
void ui_lock_now(void);
void ui_draw_icon(struct app *a, int x, int y, int s);
void ui_apply_overlays(void);  /* oscuramento software, indicatore volume */

/* Elementi grafici comuni */
void ui_status_bar(int dark_text);
void ui_home_indicator(int dark);

/* Info di sistema (sys.c) */
int  sys_battery(int *percent, int *charging);  /* 0 se non c'è batteria */
int  sys_backlight_get(void);                   /* 0..100, -1 se assente */
void sys_backlight_set(int percent);
void sys_screen_power(int on);
void sys_model(char *buf, int n);
void sys_kernel(char *buf, int n);
void sys_memory(long *total_mb, long *free_mb);
long sys_uptime(void);
int  sys_cpus(void);
int  sys_load_int(const char *key, int def);   /* /data/settings/<key> */
void sys_save_int(const char *key, int v);
int  sys_data_volatile(void);                   /* 1 se /data è solo in RAM */
void sys_net_set(int up);                       /* accende/spegne la rete */
int  sys_net_up(char *name, int n);             /* rete attiva? (nome) */

/* Testo accorciato con "…" per stare in maxw pixel */
void text_fit(const char *s, int px, enum font_weight w, int maxw, char *out, int n);

/* Numeri casuali semplici */
unsigned ui_rand(void);

#endif
