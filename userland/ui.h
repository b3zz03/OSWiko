/* OSWiko - interfaccia di sistema (SpringBoard) e API per le app */
#ifndef OSWIKO_UI_H
#define OSWIKO_UI_H

#include "gfx.h"
#include "input.h"

#define OSWIKO_VERSION "0.1"

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
	int light;                              /* sfondo chiaro: barra di stato scura */
};

extern struct app app_clock, app_calc, app_paint, app_memory, app_bubbles,
	app_torch, app_settings, app_jump, app_runner;

/* Servizi per le app */
void ui_redraw(void);              /* chiede di ridisegnare lo schermo */
void ui_timer(int ms);             /* chiama app->timer() tra ms (0 = annulla) */
void ui_go_home(void);
void ui_power(int reboot);         /* spegne (0) o riavvia (1) */
long long ui_now_ms(void);
int  ui_status_h(void);            /* altezza barra di stato in pixel */

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

/* Numeri casuali semplici */
unsigned ui_rand(void);

#endif
