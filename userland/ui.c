/* OSWiko - interfaccia di sistema: blocco schermo, home, dock, gesture.
 *
 * Modello a eventi: si ridisegna solo quando serve (tocco, tasto, timer o
 * cambio di minuto), così la CPU resta ferma il più possibile.
 */
#include "ui.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <time.h>
#include <unistd.h>

static struct app *grid_apps[] = { &app_runner, &app_jump, &app_bubbles, &app_memory,
				    &app_camera, &app_gallery, &app_clock, &app_calc };
static struct app *dock_apps[] = { &app_chat, &app_paint, &app_torch, &app_settings };
#define N_GRID ((int)(sizeof(grid_apps) / sizeof(grid_apps[0])))
#define N_DOCK ((int)(sizeof(dock_apps) / sizeof(dock_apps[0])))

enum state { ST_LOCK, ST_HOME, ST_APP, ST_OFF };

static enum state st = ST_LOCK;
static struct app *cur;
static int need_redraw = 1;
static long long timer_at;
static struct image wall;    /* sfondo, generato all'avvio */
static struct image frame;   /* home pre-renderizzata per le animazioni */
static int pressed = -1;     /* icona premuta sulla home */
static int drag_y0 = -1, drag_dy;
static int gesture_y0 = -1;

/* file sorvegliati (es. inotify dei messaggi) */
static struct { int fd; void (*cb)(int); } watches[4];
static int n_watches;

/* banner di notifica */
static struct app *banner_app;
static char banner_title[64], banner_text[256];
static long long banner_until;

static const char *giorni[] = { "domenica", "lunedì", "martedì", "mercoledì",
				"giovedì", "venerdì", "sabato" };
static const char *mesi[] = { "gennaio", "febbraio", "marzo", "aprile",
			      "maggio", "giugno", "luglio", "agosto",
			      "settembre", "ottobre", "novembre", "dicembre" };

/* ---------------- servizi ---------------- */

long long ui_now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static unsigned rng_state = 2463534242u;
unsigned ui_rand(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

void ui_redraw(void) { need_redraw = 1; }
int ui_is_open(struct app *a) { return st == ST_APP && cur == a; }
void ui_badges_changed(void) { if (st == ST_HOME) need_redraw = 1; }

void ui_watch_fd(int fd, void (*cb)(int))
{
	if (n_watches < 4) {
		watches[n_watches].fd = fd;
		watches[n_watches].cb = cb;
		n_watches++;
		input_watch_fd(fd);
	}
}

void ui_notify(struct app *a, const char *title, const char *text)
{
	banner_app = a;
	snprintf(banner_title, sizeof(banner_title), "%s", title);
	snprintf(banner_text, sizeof(banner_text), "%s", text);
	for (char *p = banner_text; *p; p++)
		if (*p == '\n')
			*p = ' ';
	banner_until = ui_now_ms() + 4500;
	need_redraw = 1;
}

void text_fit(const char *s, int px, enum font_weight w, int maxw, char *out, int n)
{
	snprintf(out, n, "%s", s);
	if (text_width(out, px, w) <= maxw)
		return;
	int len = strlen(out);
	while (len > 0) {
		/* togli un carattere UTF-8 intero */
		do
			len--;
		while (len > 0 && (out[len] & 0xc0) == 0x80);
		out[len] = 0;
		char tmp[512];
		snprintf(tmp, sizeof(tmp), "%s…", out);
		if (text_width(tmp, px, w) <= maxw) {
			snprintf(out, n, "%s", tmp);
			return;
		}
	}
}
void ui_timer(int ms) { timer_at = ms > 0 ? ui_now_ms() + ms : 0; }
int ui_status_h(void) { return P(44); }

static struct tm now_tm(void)
{
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	return tm;
}

/* ---------------- elementi comuni ---------------- */

static void status_bar(int dark, int show_time)
{
	color_t c = dark ? BLACK : WHITE;
	char buf[16];
	struct tm tm = now_tm();
	int pct, charging;

	if (show_time) {
		snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
		text_draw(P(30), P(14), buf, P(16), FONT_SEMIBOLD, c, 255);
	}
	if (sys_battery(&pct, &charging)) {
		int bw = P(25), bh = P(12), bx = G.w - P(26) - bw, by = P(17);
		gfx_rrect(bx, by, bw, bh, P(3.5), c, 90);
		gfx_rrect(bx + P(2), by + P(2), (bw - P(4)) * pct / 100, bh - P(4),
			  P(2), charging ? RGB(52, 199, 89) : c, 255);
		gfx_rrect(bx + bw + P(1), by + P(4), P(2), P(4), P(1), c, 120);
		snprintf(buf, sizeof(buf), "%d%%", pct);
		text_right(bx - P(5), P(15), buf, P(13), FONT_SEMIBOLD, c, 255);
	}
}

void ui_status_bar(int dark_text) { status_bar(dark_text, 1); }

void ui_home_indicator(int dark)
{
	int w = P(134), h = P(5);
	gfx_rrect((G.w - w) / 2, G.h - P(9) - h, w, h, h / 2, dark ? BLACK : WHITE, 200);
}

/* ---------------- sfondo ---------------- */

static void make_wallpaper(void)
{
	float w = G.w, h = G.h;

	gfx_vgradient(0, 0, G.w, G.h, RGB(20, 26, 80), RGB(70, 20, 96));
	gfx_circle(w * 0.10f, h * 0.22f, w * 0.55f, RGB(255, 84, 130), 220);
	gfx_circle(w * 1.00f, h * 0.42f, w * 0.62f, RGB(60, 130, 255), 220);
	gfx_circle(w * 0.25f, h * 0.88f, w * 0.60f, RGB(255, 160, 50), 190);
	gfx_circle(w * 0.85f, h * 0.02f, w * 0.40f, RGB(130, 90, 255), 220);
	gfx_circle(w * 0.95f, h * 0.95f, w * 0.35f, RGB(30, 200, 200), 160);
	img_capture(&wall);
	img_blur(&wall, G.w / 7);
}

/* ---------------- icone ---------------- */

static void draw_icon(struct app *a, int x, int y, int s, int is_pressed)
{
	int r = (int)(s * 0.225f);
	gfx_shadow(x, y, s, s, r, P(5), 70);
	gfx_rrect_grad(x, y, s, s, r, a->icon_top, a->icon_bottom, 255);
	if (a->icon)
		a->icon(x, y, s);
	if (is_pressed)
		gfx_rrect(x, y, s, s, r, BLACK, 90);
	if (a->badge > 0) {
		char b[8];
		snprintf(b, sizeof(b), "%d", a->badge > 99 ? 99 : a->badge);
		int bw = text_width(b, P(14), FONT_SEMIBOLD) + P(12);
		if (bw < P(22))
			bw = P(22);
		gfx_rrect(x + s - bw + P(6), y - P(6), bw, P(22), P(11), RGB(255, 59, 48), 255);
		text_center(x + s - bw / 2 + P(6), y - P(4), b, P(14), FONT_SEMIBOLD, WHITE, 255);
	}
}

/* Banner di notifica in alto, sopra a tutto */
static void draw_banner(void)
{
	if (!banner_app || ui_now_ms() >= banner_until || st == ST_OFF)
		return;
	int x = P(8), y = P(8), w = G.w - P(16), h = P(70);
	char line[256];
	gfx_shadow(x, y, w, h, P(20), P(8), 70);
	gfx_rrect(x, y, w, h, P(20), RGB(250, 250, 252), 245);
	int s = P(38);
	int badge = banner_app->badge;
	banner_app->badge = 0; /* niente pallino rosso dentro il banner */
	draw_icon(banner_app, x + P(14), y + (h - s) / 2, s, 0);
	banner_app->badge = badge;
	int tx = x + P(14) + s + P(12), tw = w - (tx - x) - P(14);
	text_fit(banner_title, P(15), FONT_SEMIBOLD, tw, line, sizeof(line));
	text_draw(tx, y + P(14), line, P(15), FONT_SEMIBOLD, BLACK, 255);
	text_fit(banner_text, P(15), FONT_REGULAR, tw, line, sizeof(line));
	text_draw(tx, y + P(36), line, P(15), FONT_REGULAR, RGB(60, 60, 67), 255);
}

/* Posizione dell'icona i (prima la griglia, poi il dock). */
static struct app *icon_slot(int i, int *x, int *y, int *s)
{
	*s = P(62);
	if (i < N_GRID) {
		int gap = (G.w - 4 * *s) / 5;
		*x = gap + (i % 4) * (*s + gap);
		*y = P(70) + (i / 4) * P(104);
		return grid_apps[i];
	}
	i -= N_GRID;
	int dw = G.w - P(24), dx = P(12);
	int gap = (dw - N_DOCK * *s) / (N_DOCK + 1);
	*x = dx + gap + i * (*s + gap);
	*y = G.h - P(16) - P(96) + (P(96) - *s) / 2;
	return dock_apps[i];
}

/* ---------------- schermate ---------------- */

static void draw_home(void)
{
	int x, y, s;

	gfx_blit(&wall);
	status_bar(0, 1);
	/* dock in vetro */
	gfx_rrect(P(12), G.h - P(16) - P(96), G.w - P(24), P(96), P(34), WHITE, 55);
	for (int i = 0; i < N_GRID + N_DOCK; i++) {
		struct app *a = icon_slot(i, &x, &y, &s);
		draw_icon(a, x, y, s, i == pressed);
		if (i < N_GRID) {
			int ly = y + s + P(7);
			text_center(x + s / 2 + 1, ly + 1, a->name, P(12.5), FONT_REGULAR, BLACK, 70);
			text_center(x + s / 2, ly, a->name, P(12.5), FONT_REGULAR, WHITE, 255);
		}
	}
	/* indicatore di pagina */
	gfx_circle(G.w / 2.0f, G.h - P(16) - P(96) - P(18), P(3.5), WHITE, 230);
}

static void draw_lock(void)
{
	struct tm tm = now_tm();
	char buf[64];
	int off = drag_dy < 0 ? drag_dy : 0;
	int fade = 255 + off * 255 / P(200);

	if (fade < 0)
		fade = 0;
	gfx_blit(&wall);
	gfx_rect(0, 0, G.w, G.h, BLACK, 50);
	status_bar(0, 0);
	snprintf(buf, sizeof(buf), "%s %d %s", giorni[tm.tm_wday], tm.tm_mday,
		 mesi[tm.tm_mon]);
	text_center(G.w / 2, P(96) + off, buf, P(21), FONT_SEMIBOLD, WHITE, fade * 9 / 10);
	snprintf(buf, sizeof(buf), "%d:%02d", tm.tm_hour, tm.tm_min);
	text_center(G.w / 2, P(118) + off, buf, P(104), FONT_SEMIBOLD, WHITE, fade);
	text_center(G.w / 2, G.h - P(52) + off / 2, "Scorri verso l'alto per aprire",
		    P(15), FONT_REGULAR, WHITE, fade * 3 / 4);
	ui_home_indicator(0);
}

static void draw(void)
{
	switch (st) {
	case ST_OFF:
		gfx_clear(BLACK);
		break;
	case ST_LOCK:
		draw_lock();
		break;
	case ST_HOME:
		draw_home();
		break;
	case ST_APP:
		cur->draw();
		ui_status_bar(cur->light);
		ui_home_indicator(cur->light);
		break;
	}
	draw_banner();
}

/* Animazione "zoom" dall'icona allo schermo intero (o al contrario). */
static void zoom_anim(struct app *a, int icon_index, int opening)
{
	int x, y, s;
	const int frames = 9;

	icon_slot(icon_index, &x, &y, &s);
	draw_home();
	img_capture(&frame);
	for (int f = 1; f <= frames; f++) {
		float t = (float)f / frames;
		t = 1 - (1 - t) * (1 - t) * (1 - t); /* ease-out */
		if (!opening)
			t = 1 - t;
		int rx = x + (int)((0 - x) * t), ry = y + (int)((0 - y) * t);
		int rw = s + (int)((G.w - s) * t), rh = s + (int)((G.h - s) * t);
		int rr = (int)(s * 0.225f + (P(40) - s * 0.225f) * t);
		gfx_blit(&frame);
		gfx_rrect_grad(rx, ry, rw, rh, rr, a->icon_top, a->icon_bottom, 255);
		gfx_present();
		usleep(10000);
	}
}

static int app_index(struct app *a)
{
	for (int i = 0; i < N_GRID; i++)
		if (grid_apps[i] == a)
			return i;
	for (int i = 0; i < N_DOCK; i++)
		if (dock_apps[i] == a)
			return N_GRID + i;
	return 0;
}

static void open_app(struct app *a)
{
	zoom_anim(a, app_index(a), 1);
	cur = a;
	timer_at = 0;
	st = ST_APP;
	if (a->open)
		a->open();
	need_redraw = 1;
}

void ui_open(struct app *a)
{
	if (st == ST_APP && cur == a) {
		if (a->open)
			a->open();
		need_redraw = 1;
		return;
	}
	if (st == ST_APP) {
		timer_at = 0;
		if (cur->close)
			cur->close();
	}
	st = ST_HOME;
	open_app(a);
}

void ui_go_home(void)
{
	if (st != ST_APP)
		return;
	timer_at = 0;
	if (cur->close)
		cur->close();
	zoom_anim(cur, app_index(cur), 0);
	st = ST_HOME;
	need_redraw = 1;
}

void ui_power(int reboot_)
{
	gfx_clear(BLACK);
	text_center(G.w / 2, G.h / 2 - P(12), reboot_ ? "Riavvio…" : "Spegnimento…",
		    P(20), FONT_REGULAR, WHITE, 255);
	gfx_present();
	sync();
	sleep(1);
	reboot(reboot_ ? RB_AUTOBOOT : RB_POWER_OFF);
	need_redraw = 1; /* se fallisce (non siamo root), torna indietro */
}

static void screen_toggle(void)
{
	if (st == ST_OFF) {
		sys_screen_power(1);
		st = ST_LOCK;
	} else {
		if (st == ST_APP && cur->close)
			cur->close();
		timer_at = 0;
		st = ST_OFF;
		draw();
		gfx_present();
		sys_screen_power(0);
	}
	need_redraw = 1;
}

/* ---------------- input ---------------- */

static void home_touch(const struct event *ev)
{
	int x, y, s;

	if (ev->type == IN_TOUCH_DOWN || ev->type == IN_TOUCH_MOVE) {
		int hit = -1;
		for (int i = 0; i < N_GRID + N_DOCK; i++) {
			icon_slot(i, &x, &y, &s);
			if (ev->x >= x && ev->x < x + s && ev->y >= y && ev->y < y + s)
				hit = i;
		}
		if (ev->type == IN_TOUCH_DOWN || hit != pressed) {
			pressed = ev->type == IN_TOUCH_DOWN ? hit : -1;
			need_redraw = 1;
		}
	} else if (ev->type == IN_TOUCH_UP) {
		int p = pressed;
		pressed = -1;
		need_redraw = 1;
		if (p >= 0)
			open_app(icon_slot(p, &x, &y, &s));
	}
}

static void lock_touch(const struct event *ev)
{
	if (ev->type == IN_TOUCH_DOWN) {
		drag_y0 = ev->y;
		drag_dy = 0;
	} else if (ev->type == IN_TOUCH_MOVE && drag_y0 >= 0) {
		drag_dy = ev->y - drag_y0;
		need_redraw = 1;
	} else if (ev->type == IN_TOUCH_UP) {
		if (drag_y0 >= 0 && drag_y0 - ev->y > P(80))
			st = ST_HOME;
		drag_y0 = -1;
		drag_dy = 0;
		need_redraw = 1;
	}
}

static void open_app(struct app *a);

static void dispatch(const struct event *ev)
{
	if (ev->type == IN_FD) {
		for (int i = 0; i < n_watches; i++)
			if (watches[i].fd == ev->code)
				watches[i].cb(ev->code);
		return;
	}
	if (ev->type == IN_KEY_UP || ev->type == IN_KEY_REPEAT) {
		if (st == ST_APP && cur->event)
			cur->event(ev);
		return;
	}
	/* tocco sul banner: apre l'app che l'ha mandato */
	if (ev->type == IN_TOUCH_DOWN && banner_app && ui_now_ms() < banner_until &&
	    st != ST_OFF && ev->y < P(80)) {
		struct app *a = banner_app;
		banner_until = 0;
		if (st == ST_APP && cur != a)
			ui_go_home();
		if (st != ST_APP || cur != a) {
			st = ST_HOME;
			open_app(a);
		}
		need_redraw = 1;
		return;
	}
	if (ev->type == IN_KEY) {
		switch (ev->code) {
		case KEY_POWER:
		case KEY_F12:
			screen_toggle();
			return;
		case KEY_ESC:
		case KEY_HOMEPAGE:
		case KEY_BACK:
		case KEY_F1:
			if (st == ST_APP)
				ui_go_home();
			else if (st == ST_LOCK)
				st = ST_HOME, need_redraw = 1;
			return;
		case KEY_VOLUMEUP:
		case KEY_VOLUMEDOWN: {
			int b = sys_backlight_get();
			if (b >= 0)
				sys_backlight_set(b + (ev->code == KEY_VOLUMEUP ? 10 : -10));
			need_redraw = 1;
			return;
		}
		}
		if (st == ST_LOCK && (ev->code == KEY_ENTER || ev->code == KEY_SPACE)) {
			st = ST_HOME;
			need_redraw = 1;
		} else if (st == ST_APP && cur->event) {
			cur->event(ev);
		}
		return;
	}

	switch (st) {
	case ST_OFF:
		return;
	case ST_LOCK:
		lock_touch(ev);
		return;
	case ST_HOME:
		home_touch(ev);
		return;
	case ST_APP:
		break;
	}
	/* Gesture di sistema: trascina dal bordo inferiore verso l'alto = home */
	if (ev->type == IN_TOUCH_DOWN && ev->y > G.h - P(26)) {
		gesture_y0 = ev->y;
		return;
	}
	if (gesture_y0 >= 0) {
		if (ev->type == IN_TOUCH_UP) {
			int y0 = gesture_y0;
			gesture_y0 = -1;
			if (y0 - ev->y > P(60))
				ui_go_home();
		}
		return;
	}
	if (cur->event)
		cur->event(ev);
}

/* ---------------- main ---------------- */

int main(void)
{
	int tries = 0;

	setvbuf(stderr, NULL, _IONBF, 0);
	while (gfx_init("/dev/fb0") < 0) {
		if (++tries > 50) {
			fprintf(stderr, "ui: nessun display, esco\n");
			return 1;
		}
		usleep(100000);
	}
	if (text_init("/usr/share/fonts") == 0)
		fprintf(stderr, "ui: nessun font caricato!\n");
	rng_state ^= (unsigned)time(NULL) ^ (unsigned)ui_now_ms();

	/* schermata di avvio mentre generiamo lo sfondo */
	gfx_clear(BLACK);
	text_center(G.w / 2, G.h / 2 - P(24), "OSWiko", P(40), FONT_SEMIBOLD, WHITE, 255);
	gfx_present();

	input_init(G.w, G.h);
	if (img_alloc(&wall, G.w, G.h) < 0 || img_alloc(&frame, G.w, G.h) < 0)
		return 1;
	make_wallpaper();

	for (int i = 0; i < N_GRID; i++)
		if (grid_apps[i]->init)
			grid_apps[i]->init();
	for (int i = 0; i < N_DOCK; i++)
		if (dock_apps[i]->init)
			dock_apps[i]->init();

	int last_min = -1;
	int banner_shown = 0;
	for (;;) {
		if (need_redraw) {
			need_redraw = 0;
#ifdef PERF
			long long t0 = ui_now_ms();
#endif
			draw();
#ifdef PERF
			long long t1 = ui_now_ms();
#endif
			gfx_present();
#ifdef PERF
			static long long sum_d, sum_p;
			static int nf;
			sum_d += t1 - t0;
			sum_p += ui_now_ms() - t1;
			if (++nf == 30) {
				fprintf(stderr, "perf: disegno %lld ms, copia %lld ms\n",
					sum_d / nf, sum_p / nf);
				sum_d = sum_p = nf = 0;
			}
#endif
		}
		long long now = ui_now_ms();
		int timeout = 1000 - (int)(now % 1000);
		if (timer_at && timer_at - now < timeout)
			timeout = timer_at > now ? (int)(timer_at - now) : 0;
		if (banner_until > now) {
			banner_shown = 1;
			if (banner_until - now < timeout)
				timeout = (int)(banner_until - now) + 1;
		} else if (banner_shown) {
			banner_shown = 0; /* il banner è scaduto: togli */
			need_redraw = 1;
			continue;
		}

		struct event ev;
		if (input_wait(&ev, timeout)) {
			dispatch(&ev);
			/* svuota la coda prima di ridisegnare */
			while (input_wait(&ev, 0))
				dispatch(&ev);
		}
		if (timer_at && ui_now_ms() >= timer_at) {
			timer_at = 0;
			if (st == ST_APP && cur->timer)
				cur->timer();
		}
		struct tm tm = now_tm();
		if (tm.tm_min != last_min && st != ST_OFF) {
			last_min = tm.tm_min;
			need_redraw = 1;
		}
	}
}
