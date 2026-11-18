/* OSWiko - Centro di controllo: si apre trascinando dal bordo superiore.
 *
 * Sfondo sfocato, riquadri traslucidi: rete e modalità aereo, ora e
 * batteria, cursori di luminosità e volume, Non disturbare, scorciatoie.
 * Tenendo premuta la Fotocamera compare un menu di opzioni.
 */
#include "control.h"
#include "photos.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define ORANGE RGB(255, 149, 0)
#define BLUE   RGB(10, 132, 255)
#define PURPLE RGB(94, 92, 230)
#define TILE_A 48              /* opacità dei riquadri bianchi */
#define LONG_PRESS_MS 450

static struct image sharp, blurred;   /* schermo al momento dell'apertura */
static int have_img;
static int state;                     /* 0 chiuso, 1 trascinamento, 2 aperto */
static int pull;                      /* quanto è stato trascinato (pixel) */
static int y_start;

enum { T_NONE = -1, T_AIR, T_WIFI, T_LOCK, T_SETTINGS, T_INFO, T_GALLERY, T_CHAT,
       T_BRIGHT, T_VOLUME, T_DND, T_TORCH, T_CLOCK, T_CALC, T_CAMERA, N_TILES };

static int pressed = T_NONE;
static long long press_at;
static int menu_open;                 /* menu della Fotocamera */
static int menu_pressed = -1;
static int menu_fresh;                /* aperto ora dalla pressione lunga, dito ancora giù */
static int touch_y0, moved;

/* ---------------- geometria ---------------- */

static int C(void) { return (G.w - P(48) - 3 * P(14)) / 4; }
static int GAP(void) { return P(14); }
static int X(int col) { return P(24) + col * (C() + GAP()); }
static int Y(int row) { return P(108) + row * (C() + GAP()); }

/* Rettangolo di un riquadro. */
static void tile_rect(int t, int *x, int *y, int *w, int *h)
{
	int c = C(), g = GAP();
	switch (t) {
	case T_AIR: case T_WIFI: case T_LOCK: case T_SETTINGS: {
		/* quattro cerchi dentro il blocco connessioni 2x2 */
		int k = t - T_AIR, bs = 2 * c + g, q = bs / 2;
		*w = *h = (int)(q * 0.78f);
		*x = X(0) + (k % 2) * q + (q - *w) / 2;
		*y = Y(0) + (k / 2) * q + (q - *h) / 2;
		return;
	}
	case T_INFO:    *x = X(2); *y = Y(0); *w = *h = 2 * c + g; return;
	case T_GALLERY: *x = X(0); *y = Y(2); *w = *h = c; return;
	case T_CHAT:    *x = X(1); *y = Y(2); *w = *h = c; return;
	case T_BRIGHT:  *x = X(2); *y = Y(2); *w = c; *h = 2 * c + g; return;
	case T_VOLUME:  *x = X(3); *y = Y(2); *w = c; *h = 2 * c + g; return;
	case T_DND:     *x = X(0); *y = Y(3); *w = 2 * c + g; *h = c; return;
	default:        *x = X(t - T_TORCH); *y = Y(4); *w = *h = c; return;
	}
}

static int tile_at(int px, int py)
{
	for (int t = 0; t < N_TILES; t++) {
		int x, y, w, h;
		tile_rect(t, &x, &y, &w, &h);
		if (px >= x && px < x + w && py >= y && py < y + h)
			return t;
	}
	return T_NONE;
}

/* ---------------- icone ---------------- */

static void rot_poly(float *p, int n, float cx, float cy, float a, color_t c)
{
	float s = sinf(a), co = cosf(a);
	for (int i = 0; i < n; i++) {
		float x = p[2 * i] - cx, y = p[2 * i + 1] - cy;
		p[2 * i] = cx + x * co - y * s;
		p[2 * i + 1] = cy + x * s + y * co;
	}
	gfx_poly(p, n, c, 255);
}

void cc_icon_airplane(float cx, float cy, float s, color_t c)
{
	float body[] = { cx - 0.06f * s, cy - 0.38f * s, cx + 0.06f * s, cy - 0.38f * s,
			 cx + 0.06f * s, cy + 0.36f * s, cx - 0.06f * s, cy + 0.36f * s };
	float wings[] = { cx - 0.44f * s, cy + 0.06f * s, cx, cy - 0.16f * s, cx + 0.44f * s, cy + 0.06f * s,
			  cx + 0.44f * s, cy + 0.15f * s, cx, cy + 0.03f * s, cx - 0.44f * s, cy + 0.15f * s };
	float tail[] = { cx - 0.2f * s, cy + 0.4f * s, cx, cy + 0.27f * s, cx + 0.2f * s, cy + 0.4f * s,
			 cx + 0.2f * s, cy + 0.47f * s, cx, cy + 0.4f * s, cx - 0.2f * s, cy + 0.47f * s };
	float a = (float)M_PI / 4;
	rot_poly(body, 4, cx, cy, a, c);
	rot_poly(wings, 6, cx, cy, a, c);
	rot_poly(tail, 6, cx, cy, a, c);
	gfx_circle(cx + 0.27f * s * 0.707f, cy - 0.27f * s * 0.707f, 0.065f * s, c, 255);
}

void cc_icon_wifi(float cx, float cy, float s, color_t c)
{
	float by = cy + 0.28f * s, a0 = -3 * (float)M_PI / 4, a1 = -(float)M_PI / 4;
	gfx_circle(cx, by - 0.02f * s, 0.07f * s, c, 255);
	gfx_arc(cx, by, 0.3f * s, 0.1f * s, a0, a1, c, 255);
	gfx_arc(cx, by, 0.52f * s, 0.1f * s, a0, a1, c, 255);
}

void cc_icon_moon(float cx, float cy, float s, color_t c, color_t bg)
{
	gfx_circle(cx, cy, 0.32f * s, c, 255);
	gfx_circle(cx + 0.16f * s, cy - 0.12f * s, 0.27f * s, bg, 255);
}

static void icon_lock(float cx, float cy, float s, color_t c)
{
	gfx_arc(cx, cy - 0.06f * s, 0.2f * s, 0.07f * s, -(float)M_PI, 0, c, 255);
	gfx_rect((int)(cx - 0.2f * s), (int)(cy - 0.07f * s), (int)(0.07f * s), (int)(0.08f * s), c, 255);
	gfx_rect((int)(cx + 0.13f * s), (int)(cy - 0.07f * s), (int)(0.07f * s), (int)(0.08f * s), c, 255);
	gfx_rrect((int)(cx - 0.28f * s), (int)(cy), (int)(0.56f * s), (int)(0.4f * s), (int)(0.07f * s), c, 255);
}

static void icon_gear(float cx, float cy, float s, color_t c, color_t bg)
{
	for (int i = 0; i < 8; i++) {
		float a = i * (float)M_PI / 4;
		gfx_line(cx + cosf(a) * 0.2f * s, cy + sinf(a) * 0.2f * s,
			 cx + cosf(a) * 0.38f * s, cy + sinf(a) * 0.38f * s, 0.13f * s, c, 255);
	}
	gfx_circle(cx, cy, 0.27f * s, c, 255);
	gfx_circle(cx, cy, 0.11f * s, bg, 255);
}

static void icon_sun(float cx, float cy, float s, color_t c)
{
	gfx_circle(cx, cy, 0.17f * s, c, 255);
	for (int i = 0; i < 8; i++) {
		float a = i * (float)M_PI / 4;
		gfx_line(cx + cosf(a) * 0.28f * s, cy + sinf(a) * 0.28f * s,
			 cx + cosf(a) * 0.4f * s, cy + sinf(a) * 0.4f * s, 0.07f * s, c, 255);
	}
}

void cc_icon_speaker(float cx, float cy, float s, color_t c, int level)
{
	gfx_rect((int)(cx - 0.36f * s), (int)(cy - 0.1f * s), (int)(0.16f * s), (int)(0.2f * s), c, 255);
	float cone[] = { cx - 0.22f * s, cy - 0.1f * s, cx + 0.02f * s, cy - 0.3f * s,
			 cx + 0.02f * s, cy + 0.3f * s, cx - 0.22f * s, cy + 0.1f * s };
	gfx_poly(cone, 4, c, 255);
	if (level == 0) {
		gfx_line(cx + 0.12f * s, cy - 0.12f * s, cx + 0.36f * s, cy + 0.12f * s, 0.06f * s, c, 255);
		gfx_line(cx + 0.36f * s, cy - 0.12f * s, cx + 0.12f * s, cy + 0.12f * s, 0.06f * s, c, 255);
		return;
	}
	gfx_arc(cx + 0.02f * s, cy, 0.18f * s, 0.06f * s, -0.9f, 0.9f, c, 255);
	if (level > 50)
		gfx_arc(cx + 0.02f * s, cy, 0.33f * s, 0.06f * s, -0.9f, 0.9f, c, 255);
}

/* ---------------- apertura e chiusura ---------------- */

int cc_active(void) { return state != 0; }

static void capture(void)
{
	if (!have_img) {
		if (img_alloc(&sharp, G.w, G.h) < 0 || img_alloc(&blurred, G.w, G.h) < 0)
			return;
		have_img = 1;
	}
	img_capture(&sharp);
	memcpy(blurred.px, sharp.px, (size_t)G.w * G.h * 4);
	img_blur(&blurred, G.w / 18);
}

void cc_begin_pull(int y)
{
	capture();
	state = 1;
	pull = 0;
	y_start = y;
	menu_open = 0;
	pressed = T_NONE;
	ui_redraw();
}

void cc_close(void)
{
	state = 0;
	menu_open = 0;
	pressed = T_NONE;
	ui_redraw();
}

/* Animazione verso aperto (to=G.h/3) o chiuso (to=0). */
static void animate(int to)
{
	for (int f = 0; f < 6; f++) {
		pull += (to - pull) / 2;
		ui_redraw();
		cc_draw();
		ui_apply_overlays();
		gfx_present();
	}
	pull = to;
}

/* ---------------- disegno ---------------- */

static void toggle_circle(int t, int on, color_t on_color)
{
	int x, y, w, h;
	tile_rect(t, &x, &y, &w, &h);
	float cx = x + w / 2.0f, cy = y + h / 2.0f, r = w / 2.0f;
	color_t bg = on ? on_color : RGB(255, 255, 255);
	gfx_circle(cx, cy, r, bg, on ? 255 : 70);
	if (pressed == t)
		gfx_circle(cx, cy, r, BLACK, 60);
	color_t fg = WHITE;
	switch (t) {
	case T_AIR: cc_icon_airplane(cx, cy, w * 0.62f, fg); break;
	case T_WIFI: cc_icon_wifi(cx, cy, w * 0.62f, fg); break;
	case T_LOCK: icon_lock(cx, cy, w * 0.62f, fg); break;
	case T_SETTINGS: icon_gear(cx, cy, w * 0.62f, fg, gfx_mix(RGB(90, 90, 96), WHITE, 1, 4)); break;
	}
}

static void app_tile(int t, struct app *a)
{
	int x, y, w, h;
	tile_rect(t, &x, &y, &w, &h);
	gfx_circle(x + w / 2.0f, y + h / 2.0f, w / 2.0f, WHITE, TILE_A);
	if (pressed == t)
		gfx_circle(x + w / 2.0f, y + h / 2.0f, w / 2.0f, WHITE, 60);
	int s = (int)(w * 0.54f);
	ui_draw_icon(a, x + (w - s) / 2, y + (h - s) / 2, s);
}

static void slider(int t, int value, int is_volume)
{
	int x, y, w, h;
	tile_rect(t, &x, &y, &w, &h);
	int r = P(22);
	gfx_rrect(x, y, w, h, r, WHITE, TILE_A);
	/* parte piena dal basso, ritagliata con gli angoli arrotondati */
	int fh = h * value / 100;
	if (fh > 0) {
		int top = y + h - fh;
		for (int yy = top; yy < y + h; yy++) {
			/* larghezza della riga tenendo conto degli angoli */
			float inset = 0;
			if (yy < y + r) {
				float dy = y + r - yy - 0.5f;
				inset = r - sqrtf(fmaxf(0, r * r - dy * dy));
			} else if (yy >= y + h - r) {
				float dy = yy - (y + h - r) + 0.5f;
				inset = r - sqrtf(fmaxf(0, r * r - dy * dy));
			}
			gfx_rect(x + (int)inset, yy, w - 2 * (int)inset, 1, WHITE, 245);
		}
	}
	color_t ic = value > 18 ? RGB(80, 80, 86) : WHITE;
	if (is_volume)
		cc_icon_speaker(x + w / 2.0f, y + h - P(30), w * 0.42f, ic, value);
	else
		icon_sun(x + w / 2.0f, y + h - P(30), w * 0.5f, ic);
}

static void info_module(void)
{
	static const char *giorni[] = { "dom", "lun", "mar", "mer", "gio", "ven", "sab" };
	static const char *mesi[] = { "gen", "feb", "mar", "apr", "mag", "giu",
				      "lug", "ago", "set", "ott", "nov", "dic" };
	int x, y, w, h;
	char buf[64], net[32];
	time_t now = time(NULL);
	struct tm tm;

	tile_rect(T_INFO, &x, &y, &w, &h);
	gfx_rrect(x, y, w, h, P(26), WHITE, TILE_A);
	localtime_r(&now, &tm);
	snprintf(buf, sizeof(buf), "%s %d %s", giorni[tm.tm_wday], tm.tm_mday, mesi[tm.tm_mon]);
	text_draw(x + P(16), y + P(16), buf, P(14), FONT_SEMIBOLD, WHITE, 200);
	snprintf(buf, sizeof(buf), "%d:%02d", tm.tm_hour, tm.tm_min);
	text_draw(x + P(14), y + P(34), buf, P(44), FONT_SEMIBOLD, WHITE, 255);

	int pct, chg;
	if (sys_battery(&pct, &chg))
		snprintf(buf, sizeof(buf), "Batteria %d%%%s", pct, chg ? " (in carica)" : "");
	else
		snprintf(buf, sizeof(buf), "Alimentazione esterna");
	text_draw(x + P(16), y + h - P(52), buf, P(12), FONT_REGULAR, WHITE, 210);
	if (ui_airplane())
		snprintf(net, sizeof(net), "Modalità aereo");
	else if (!sys_net_up(net, sizeof(net)))
		snprintf(net, sizeof(net), "Rete spenta");
	text_draw(x + P(16), y + h - P(32), net, P(12), FONT_REGULAR, WHITE, 210);
}

static void dnd_pill(void)
{
	int x, y, w, h, on = ui_dnd();
	tile_rect(T_DND, &x, &y, &w, &h);
	gfx_rrect(x, y, w, h, h / 2, WHITE, on ? 235 : TILE_A);
	if (pressed == T_DND)
		gfx_rrect(x, y, w, h, h / 2, BLACK, 50);
	float cr = h * 0.36f, cx = x + h / 2.0f, cy = y + h / 2.0f;
	gfx_circle(cx, cy, cr, on ? PURPLE : RGB(120, 120, 128), 255);
	cc_icon_moon(cx, cy, cr * 1.6f, WHITE, on ? PURPLE : RGB(120, 120, 128));
	color_t tc = on ? BLACK : WHITE;
	text_draw(x + h, y + h / 2 - P(18), "Non", P(14), FONT_SEMIBOLD, tc, 255);
	text_draw(x + h, y + h / 2 - P(2), "disturbare", P(14), FONT_SEMIBOLD, tc, 255);
}

static void camera_menu(void)
{
	static const char *items[] = { "Scatta una foto", "Apri Galleria" };
	int x, y, w, h;
	tile_rect(T_CAMERA, &x, &y, &w, &h);
	int mw = P(200), mh = P(48) * 2, mx = x + w - mw, my = y - mh - P(12);
	gfx_shadow(mx, my, mw, mh, P(18), P(10), 90);
	gfx_rrect(mx, my, mw, mh, P(18), RGB(60, 60, 66), 245);
	for (int i = 0; i < 2; i++) {
		int iy = my + i * P(48);
		if (menu_pressed == i)
			gfx_rect(mx, iy, mw, P(48), WHITE, 40);
		text_draw(mx + P(18), iy + P(14), items[i], P(16), FONT_REGULAR, WHITE, 255);
		if (i == 0)
			gfx_rect(mx + P(14), iy + P(48) - 1, mw - P(28), 1, WHITE, 50);
	}
}

void cc_draw(void)
{
	if (!have_img)
		return;
	float t = state == 2 ? 1 : pull / (G.h / 3.0f);
	if (t > 1) t = 1;
	if (t < 0) t = 0;

	/* sfondo: lo schermo di prima che si sfoca man mano che si tira */
	gfx_blit(&sharp);
	for (size_t i = 0; i < (size_t)G.w * G.h; i++) {
		uint32_t d = G.back[i], c = blurred.px[i];
		int a = (int)(t * 255);
		uint32_t rb = (((c & 0xff00ff) * a + (d & 0xff00ff) * (255 - a)) >> 8) & 0xff00ff;
		uint32_t g = (((c & 0xff00) * a + (d & 0xff00) * (255 - a)) >> 8) & 0xff00;
		G.back[i] = rb | g;
	}
	gfx_rect(0, 0, G.w, G.h, BLACK, (int)(t * 110));

	/* indicatore in alto; i riquadri compaiono a pannello aperto */
	gfx_rrect(G.w / 2 - P(18), P(56), P(36), P(5), P(3), WHITE, (int)(t * 150));
	if (t < 1)
		return;
	int bs = 2 * C() + GAP();
	gfx_rrect(X(0), Y(0), bs, bs, P(26), WHITE, TILE_A);
	toggle_circle(T_AIR, ui_airplane(), ORANGE);
	toggle_circle(T_WIFI, ui_wifi() && !ui_airplane(), BLUE);
	toggle_circle(T_LOCK, 0, 0);
	toggle_circle(T_SETTINGS, 0, 0);
	info_module();
	app_tile(T_GALLERY, &app_gallery);
	app_tile(T_CHAT, &app_chat);
	slider(T_BRIGHT, ui_brightness(), 0);
	slider(T_VOLUME, ui_volume(), 1);
	dnd_pill();
	app_tile(T_TORCH, &app_torch);
	app_tile(T_CLOCK, &app_clock);
	app_tile(T_CALC, &app_calc);
	app_tile(T_CAMERA, &app_camera);
	if (menu_open)
		camera_menu();
}

/* ---------------- tocchi ---------------- */

static void set_slider(int t, int py)
{
	int x, y, w, h;
	tile_rect(t, &x, &y, &w, &h);
	int v = (y + h - py) * 100 / h;
	v = v < 0 ? 0 : v > 100 ? 100 : v;
	if (t == T_BRIGHT)
		ui_set_brightness(v);
	else
		ui_set_volume(v);
	ui_redraw();
}

static void activate(int t)
{
	switch (t) {
	case T_AIR: ui_set_airplane(!ui_airplane()); break;
	case T_WIFI: if (!ui_airplane()) ui_set_wifi(!ui_wifi()); break;
	case T_LOCK: cc_close(); ui_lock_now(); return;
	case T_SETTINGS: cc_close(); ui_open(&app_settings); return;
	case T_GALLERY: cc_close(); ui_open(&app_gallery); return;
	case T_CHAT: cc_close(); ui_open(&app_chat); return;
	case T_DND: ui_set_dnd(!ui_dnd()); break;
	case T_TORCH: cc_close(); ui_open(&app_torch); return;
	case T_CLOCK: cc_close(); ui_open(&app_clock); return;
	case T_CALC: cc_close(); ui_open(&app_calc); return;
	case T_CAMERA: cc_close(); ui_open(&app_camera); return;
	}
	ui_redraw();
}

static int menu_item_at(int px, int py)
{
	int x, y, w, h;
	tile_rect(T_CAMERA, &x, &y, &w, &h);
	int mw = P(200), mx = x + w - mw, my = y - P(96) - P(12);
	if (px < mx || px >= mx + mw || py < my || py >= my + P(96))
		return -1;
	return (py - my) / P(48);
}

void cc_event(const struct event *ev)
{
	if (state == 1) { /* si sta tirando giù */
		if (ev->type == IN_TOUCH_MOVE) {
			pull = ev->y - y_start;
			ui_redraw();
		} else if (ev->type == IN_TOUCH_UP) {
			if (pull > P(70)) {
				animate(G.h / 3);
				state = 2;
			} else {
				animate(0);
				cc_close();
			}
			ui_redraw();
		}
		return;
	}
	if (menu_open) {
		if (ev->type == IN_TOUCH_DOWN || ev->type == IN_TOUCH_MOVE) {
			menu_pressed = menu_item_at(ev->x, ev->y);
			ui_redraw();
		} else if (ev->type == IN_TOUCH_UP) {
			int i = menu_item_at(ev->x, ev->y);
			if (menu_fresh && i < 0) {
				/* il dito si è alzato fuori dalle voci: il menu resta aperto */
				menu_fresh = 0;
				menu_pressed = -1;
				ui_redraw();
				return;
			}
			menu_fresh = 0;
			menu_open = 0;
			menu_pressed = -1;
			if (i == 0) { cc_close(); ui_open(&app_camera); }
			else if (i == 1) { cc_close(); ui_open(&app_gallery); }
			ui_redraw();
		}
		return;
	}
	switch (ev->type) {
	case IN_TOUCH_DOWN:
		pressed = tile_at(ev->x, ev->y);
		press_at = ui_now_ms();
		touch_y0 = ev->y;
		moved = 0;
		if (pressed == T_BRIGHT || pressed == T_VOLUME)
			set_slider(pressed, ev->y);
		ui_redraw();
		break;
	case IN_TOUCH_MOVE:
		if (abs(ev->y - touch_y0) > P(10))
			moved = 1;
		if (pressed == T_BRIGHT || pressed == T_VOLUME)
			set_slider(pressed, ev->y);
		break;
	case IN_TOUCH_UP: {
		int t = pressed;
		pressed = T_NONE;
		if (t == T_BRIGHT || t == T_VOLUME) {
			ui_redraw();
			break;
		}
		if (t == T_NONE) {
			/* tocco nel vuoto o trascinamento verso l'alto: chiudi */
			animate(0);
			cc_close();
			break;
		}
		if (tile_at(ev->x, ev->y) == t && !moved)
			activate(t);
		ui_redraw();
		break;
	}
	default:
		break;
	}
}

/* Chiamata dal ciclo eventi: pressione lunga sulla Fotocamera. */
int cc_next_ms(void)
{
	if (state == 2 && pressed == T_CAMERA && !menu_open) {
		long long left = press_at + LONG_PRESS_MS - ui_now_ms();
		return left > 0 ? (int)left : 0;
	}
	return -1;
}

void cc_tick(void)
{
	if (state == 2 && pressed == T_CAMERA && !menu_open &&
	    ui_now_ms() - press_at >= LONG_PRESS_MS) {
		menu_open = 1;
		menu_fresh = 1;
		pressed = T_NONE;
		ui_redraw();
	}
}
