/* OSWiko - Calcolatrice */
#include "ui.h"

#include <ctype.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ORANGE RGB(255, 159, 10)
#define LGRAY  RGB(165, 165, 165)
#define DGRAY  RGB(51, 51, 51)

static const char *labels[5][4] = {
	{ "AC", "±", "%", "÷" },
	{ "7", "8", "9", "×" },
	{ "4", "5", "6", "−" },
	{ "1", "2", "3", "+" },
	{ "0", "0", ",", "=" },
};

static char display[32] = "0";
static double acc;
static int op;         /* 0, '+', '-', '*', '/' */
static int fresh = 1;  /* il prossimo numero inizia da capo */
static int pressed_r = -1, pressed_c = -1;

static void show(double v)
{
	if (isnan(v) || isinf(v)) {
		strcpy(display, "Errore");
		return;
	}
	snprintf(display, sizeof(display), "%.9g", v);
	for (char *p = display; *p; p++)
		if (*p == '.')
			*p = ',';
}

static double value(void)
{
	char tmp[32];
	strcpy(tmp, display);
	for (char *p = tmp; *p; p++)
		if (*p == ',')
			*p = '.';
	return atof(tmp);
}

static double apply(double a, double b, int o)
{
	switch (o) {
	case '+': return a + b;
	case '-': return a - b;
	case '*': return a * b;
	case '/': return a / b;
	}
	return b;
}

static void press(const char *k)
{
	if (isdigit((unsigned char)k[0])) {
		if (fresh || !strcmp(display, "0") || !strcmp(display, "Errore")) {
			display[0] = 0;
			fresh = 0;
		}
		if (strlen(display) < 9)
			strcat(display, k);
	} else if (!strcmp(k, ",")) {
		if (fresh) {
			strcpy(display, "0");
			fresh = 0;
		}
		if (!strchr(display, ','))
			strcat(display, ",");
	} else if (!strcmp(k, "AC")) {
		strcpy(display, "0");
		acc = 0;
		op = 0;
		fresh = 1;
	} else if (!strcmp(k, "±")) {
		show(-value());
	} else if (!strcmp(k, "%")) {
		show(value() / 100);
	} else {
		int o = !strcmp(k, "+") ? '+' : !strcmp(k, "−") ? '-' :
			!strcmp(k, "×") ? '*' : !strcmp(k, "÷") ? '/' : '=';
		if (op && !fresh) {
			acc = apply(acc, value(), op);
			show(acc);
		} else if (!op) {
			acc = value();
		}
		op = o == '=' ? 0 : o;
		fresh = 1;
	}
}

static void layout(int *bs, int *gap, int *y0)
{
	*gap = P(13);
	*bs = (G.w - 5 * *gap) / 4;
	*y0 = G.h - P(30) - 5 * *bs - 4 * *gap;
}

static void draw(void)
{
	int bs, gap, y0;
	layout(&bs, &gap, &y0);
	gfx_clear(BLACK);

	int px = P(86);
	while (text_width(display, px, FONT_LIGHT) > G.w - 2 * gap && px > P(30))
		px -= P(4);
	text_right(G.w - gap - P(8), y0 - P(16) - px, display, px, FONT_LIGHT, WHITE, 255);

	for (int r = 0; r < 5; r++)
		for (int c = 0; c < 4; c++) {
			if (r == 4 && c == 1)
				continue; /* lo 0 occupa due colonne */
			const char *k = labels[r][c];
			int x = gap + c * (bs + gap), y = y0 + r * (bs + gap);
			int w = (r == 4 && c == 0) ? 2 * bs + gap : bs;
			color_t bg = r == 0 && c < 3 ? LGRAY : c == 3 ? ORANGE : DGRAY;
			color_t fg = r == 0 && c < 3 ? BLACK : WHITE;
			int is_op = c == 3 && r < 4 && op && fresh &&
				    ((op == '/' && r == 0) || (op == '*' && r == 1) ||
				     (op == '-' && r == 2) || (op == '+' && r == 3));
			if (is_op) {
				bg = WHITE;
				fg = ORANGE;
			}
			if (r == pressed_r && (c == pressed_c || (r == 4 && pressed_c <= 1 && c == 0)))
				bg = gfx_mix(bg, WHITE, 1, 3);
			gfx_rrect(x, y, w, bs, bs / 2, bg, 255);
			int fs = c == 3 ? P(40) : r == 0 ? P(30) : P(36);
			int ty = y + (bs - fs) / 2 - P(2);
			if (w > bs)
				text_draw(x + bs / 2 - text_width(k, fs, FONT_REGULAR) / 2, ty,
					  k, fs, FONT_REGULAR, fg, 255);
			else
				text_center(x + bs / 2, ty, k, fs, FONT_REGULAR, fg, 255);
		}
}

static void event(const struct event *ev)
{
	int bs, gap, y0;
	layout(&bs, &gap, &y0);

	if (ev->type == IN_TOUCH_DOWN) {
		int c = (ev->x - gap / 2) / (bs + gap), r = (ev->y - y0 + gap / 2) / (bs + gap);
		if (ev->y >= y0 - gap / 2 && r >= 0 && r < 5 && c >= 0 && c < 4) {
			pressed_r = r;
			pressed_c = c;
			ui_redraw();
		}
	} else if (ev->type == IN_TOUCH_UP && pressed_r >= 0) {
		press(labels[pressed_r][pressed_c]);
		pressed_r = pressed_c = -1;
		ui_redraw();
	} else if (ev->type == IN_KEY) {
		/* tastiera del PC (utile nell'emulatore) */
		static const struct { int code; const char *k; } map[] = {
			{ KEY_0, "0" }, { KEY_1, "1" }, { KEY_2, "2" }, { KEY_3, "3" },
			{ KEY_4, "4" }, { KEY_5, "5" }, { KEY_6, "6" }, { KEY_7, "7" },
			{ KEY_8, "8" }, { KEY_9, "9" }, { KEY_KP0, "0" }, { KEY_KP1, "1" },
			{ KEY_KP2, "2" }, { KEY_KP3, "3" }, { KEY_KP4, "4" }, { KEY_KP5, "5" },
			{ KEY_KP6, "6" }, { KEY_KP7, "7" }, { KEY_KP8, "8" }, { KEY_KP9, "9" },
			{ KEY_KPPLUS, "+" }, { KEY_KPMINUS, "−" }, { KEY_MINUS, "−" },
			{ KEY_KPASTERISK, "×" }, { KEY_KPSLASH, "÷" }, { KEY_SLASH, "÷" },
			{ KEY_ENTER, "=" }, { KEY_KPENTER, "=" }, { KEY_EQUAL, "=" },
			{ KEY_COMMA, "," }, { KEY_DOT, "," }, { KEY_KPDOT, "," },
			{ KEY_BACKSPACE, "AC" }, { KEY_DELETE, "AC" },
		};
		for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
			if (map[i].code == ev->code) {
				press(map[i].k);
				ui_redraw();
			}
	}
}

static void icon(int x, int y, int s)
{
	static const char *sym[4] = { "+", "−", "×", "=" };
	float d = s * 0.25f, r = s * 0.17f;
	for (int i = 0; i < 4; i++) {
		float cx = x + s / 2.0f + (i % 2 ? d : -d) * 0.95f;
		float cy = y + s / 2.0f + (i / 2 ? d : -d) * 0.95f;
		gfx_circle(cx, cy, r, i == 3 ? ORANGE : i == 0 ? LGRAY : DGRAY, 255);
		int fs = (int)(r * 1.7f);
		text_center((int)cx, (int)(cy - fs * 0.62f), sym[i], fs, FONT_SEMIBOLD,
			    i == 0 ? BLACK : WHITE, 255);
	}
}

struct app app_calc = {
	.name = "Calcolatrice",
	.icon_top = RGB(70, 70, 74), .icon_bottom = RGB(28, 28, 30),
	.icon = icon, .draw = draw, .event = event,
};
