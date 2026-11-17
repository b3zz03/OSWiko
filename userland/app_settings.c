/* OSWiko - Impostazioni: info sul dispositivo, luminosità, spegnimento */
#include "ui.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define BG     RGB(242, 242, 247)
#define LABEL  RGB(0, 0, 0)
#define VALUE  RGB(138, 138, 142)
#define SEP    RGB(220, 220, 224)
#define BLUE   RGB(0, 122, 255)
#define RED    RGB(255, 59, 48)

static int row_h(void) { return P(46); }

/* Coordinate dei controlli, calcolate in draw() e usate negli eventi. */
static int slider_y = -1, slider_x0, slider_x1;
static int reboot_y, poweroff_y;
static int dragging;
static int scroll, content_h;           /* scorrimento verticale */
static int touch_y0, scroll0, moved;

static int group(int y, const char *title, int rows)
{
	if (title)
		text_draw(P(32), y, title, P(13), FONT_REGULAR, VALUE, 255);
	y += P(22);
	gfx_rrect(P(16), y, G.w - P(32), rows * row_h(), P(12), WHITE, 255);
	return y;
}

static void row(int y, int i, int last, const char *label, const char *value)
{
	int ry = y + i * row_h();
	text_draw(P(32), ry + P(13), label, P(17), FONT_REGULAR, LABEL, 255);
	if (value)
		text_right(G.w - P(32), ry + P(13), value, P(17), FONT_REGULAR, VALUE, 255);
	if (!last)
		gfx_rect(P(32), ry + row_h() - 1, G.w - P(48), 1, SEP, 255);
}

static void draw(void)
{
	char model[64], kernel[64], mem[48], cpu[32], scr[32], up[32], bat[32];
	long tot, fre, s = sys_uptime();
	int pct, chg;

	sys_model(model, sizeof(model));
	sys_kernel(kernel, sizeof(kernel));
	sys_memory(&tot, &fre);
	snprintf(mem, sizeof(mem), "%ld MB liberi su %ld", fre, tot);
	snprintf(cpu, sizeof(cpu), "%d core", sys_cpus());
	snprintf(scr, sizeof(scr), "%d × %d", G.w, G.h);
	if (s >= 3600)
		snprintf(up, sizeof(up), "%ld h %ld min", s / 3600, s / 60 % 60);
	else
		snprintf(up, sizeof(up), "%ld min %ld s", s / 60, s % 60);
	if (sys_battery(&pct, &chg))
		snprintf(bat, sizeof(bat), "%d%%%s", pct, chg ? " (in carica)" : "");
	else
		snprintf(bat, sizeof(bat), "Non presente");

	gfx_clear(BG);
	text_draw(P(20), P(52) - scroll, "Impostazioni", P(34), FONT_SEMIBOLD, LABEL, 255);

	int y = group(P(104) - scroll, "DISPOSITIVO", 5);
	row(y, 0, 0, "Modello", model);
	row(y, 1, 0, "Processore", cpu);
	row(y, 2, 0, "Memoria", mem);
	row(y, 3, 0, "Schermo", scr);
	row(y, 4, 1, "Batteria", bat);

	y = group(y + 5 * row_h() + P(14), "SISTEMA", 3);
	row(y, 0, 0, "Versione", "OSWiko " OSWIKO_VERSION);
	row(y, 1, 0, "Kernel", kernel);
	row(y, 2, 1, "Acceso da", up);

	y = group(y + 3 * row_h() + P(14), "LUMINOSITÀ", 1);
	int bl = sys_backlight_get();
	slider_x0 = P(40);
	slider_x1 = G.w - P(40);
	if (bl < 0) {
		row(y, 0, 1, "Non regolabile", NULL);
		slider_y = -1;
	} else {
		slider_y = y + row_h() / 2;
		int kx = slider_x0 + (slider_x1 - slider_x0) * bl / 100;
		gfx_rrect(slider_x0, slider_y - P(2), slider_x1 - slider_x0, P(4), P(2), SEP, 255);
		gfx_rrect(slider_x0, slider_y - P(2), kx - slider_x0, P(4), P(2), BLUE, 255);
		gfx_circle(kx, slider_y + P(1), P(14), BLACK, 30);
		gfx_circle(kx, slider_y, P(13), WHITE, 255);
	}

	y = group(y + row_h() + P(14), NULL, 2);
	reboot_y = y;
	poweroff_y = y + row_h();
	text_draw(P(32), y + P(13), "Riavvia", P(17), FONT_REGULAR, BLUE, 255);
	gfx_rect(P(32), y + row_h() - 1, G.w - P(48), 1, SEP, 255);
	text_draw(P(32), y + row_h() + P(13), "Spegni", P(17), FONT_REGULAR, RED, 255);
	content_h = y + 2 * row_h() + P(40) + scroll;

	/* barra di stato opaca sopra il contenuto che scorre */
	gfx_rect(0, 0, G.w, ui_status_h(), BG, 255);
}

static void clamp_scroll(void)
{
	int max = content_h - G.h;
	if (scroll > max)
		scroll = max;
	if (scroll < 0)
		scroll = 0;
}

static void set_from_x(int x)
{
	int pct = (x - slider_x0) * 100 / (slider_x1 - slider_x0);
	sys_backlight_set(pct < 0 ? 0 : pct > 100 ? 100 : pct);
	ui_redraw();
}

static void event(const struct event *ev)
{
	if (ev->type == IN_TOUCH_DOWN) {
		touch_y0 = ev->y;
		scroll0 = scroll;
		moved = 0;
		if (slider_y >= 0 && abs(ev->y - slider_y) < P(24)) {
			dragging = 1;
			set_from_x(ev->x);
		}
	} else if (ev->type == IN_TOUCH_MOVE) {
		if (dragging) {
			set_from_x(ev->x);
		} else if (moved || abs(ev->y - touch_y0) > P(8)) {
			moved = 1;
			scroll = scroll0 - (ev->y - touch_y0);
			clamp_scroll();
			ui_redraw();
		}
	} else if (ev->type == IN_TOUCH_UP) {
		if (dragging || moved) {
			dragging = 0;
			return;
		}
		if (ev->y >= reboot_y && ev->y < reboot_y + row_h())
			ui_power(1);
		else if (ev->y >= poweroff_y && ev->y < poweroff_y + row_h())
			ui_power(0);
	}
}

static void open_(void) { scroll = 0; ui_timer(1000); }
static void timer(void) { ui_redraw(); ui_timer(1000); } /* aggiorna uptime */

static void icon(int x, int y, int s)
{
	float cx = x + s / 2.0f, cy = y + s / 2.0f, r = s * 0.30f;
	for (int i = 0; i < 8; i++) {
		float a = i * (float)M_PI / 4;
		gfx_line(cx + cosf(a) * r * 0.8f, cy + sinf(a) * r * 0.8f,
			 cx + cosf(a) * r * 1.18f, cy + sinf(a) * r * 1.18f,
			 s * 0.11f, RGB(240, 240, 245), 255);
	}
	gfx_circle(cx, cy, r, RGB(240, 240, 245), 255);
	gfx_circle(cx, cy, r * 0.45f, RGB(120, 120, 128), 255);
}

struct app app_settings = {
	.name = "Impostazioni",
	.icon_top = RGB(160, 160, 168), .icon_bottom = RGB(110, 110, 118),
	.icon = icon, .open = open_, .draw = draw, .event = event, .timer = timer,
	.light = 1,
};
