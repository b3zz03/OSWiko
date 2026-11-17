/* OSWiko - Disegno: tela, colori, pennelli */
#include "photos.h"
#include "ui.h"

#include <string.h>

static const color_t palette[] = {
	RGB(28, 28, 30), RGB(255, 59, 48), RGB(255, 149, 0), RGB(255, 204, 0),
	RGB(52, 199, 89), RGB(0, 122, 255), RGB(175, 82, 222), RGB(255, 255, 255),
};
#define NPAL ((int)(sizeof(palette) / sizeof(palette[0])))
static const float sizes[] = { 4, 10, 22 };

static struct image canvas;
static int have_canvas;
static int color_i = 5, size_i = 1;
static int last_x = -1, last_y;
static long long saved_until;

static int bar_y(void) { return G.h - P(124); }

static void clear_canvas(void)
{
	for (size_t i = 0; i < (size_t)canvas.w * canvas.h; i++)
		canvas.px[i] = WHITE;
}

static void open_(void)
{
	if (!have_canvas && img_alloc(&canvas, G.w, G.h) == 0) {
		have_canvas = 1;
		clear_canvas();
	}
}

/* Disegna sulla tela invece che sullo schermo. */
static void stroke(int x0, int y0, int x1, int y1)
{
	uint32_t *screen = G.back;
	G.back = canvas.px;
	gfx_line(x0, y0, x1, y1, P(sizes[size_i]), palette[color_i], 255);
	G.back = screen;
}

static void draw(void)
{
	if (!have_canvas) {
		gfx_clear(WHITE);
		return;
	}
	gfx_blit(&canvas);

	/* barra strumenti flottante */
	int y = bar_y(), h = P(84), x = P(10), w = G.w - P(20);
	gfx_shadow(x, y, w, h, P(22), P(8), 60);
	gfx_rrect(x, y, w, h, P(22), RGB(248, 248, 250), 245);

	int cs = P(30), gap = (w - P(20) - NPAL * cs) / (NPAL - 1);
	for (int i = 0; i < NPAL; i++) {
		float cx = x + P(10) + i * (cs + gap) + cs / 2.0f, cy = y + P(24);
		if (i == color_i)
			gfx_ring(cx, cy, cs / 2.0f + P(4), P(2.5), palette[i] == WHITE ?
				 RGB(160, 160, 166) : palette[i], 255);
		gfx_circle(cx, cy, cs / 2.0f, palette[i], 255);
		if (palette[i] == WHITE)
			gfx_ring(cx, cy, cs / 2.0f, P(1), RGB(200, 200, 205), 255);
	}
	for (int i = 0; i < 3; i++) {
		float cx = x + P(30) + i * P(46), cy = y + P(62);
		if (i == size_i)
			gfx_circle(cx, cy, P(17), RGB(220, 220, 226), 255);
		gfx_circle(cx, cy, P(sizes[i]) / 2.0f + 1, RGB(60, 60, 67), 255);
	}
	int bx = x + w - P(96), by = y + P(48);
	gfx_rrect(bx, by, P(86), P(28), P(14), RGB(255, 59, 48), 255);
	text_center(bx + P(43), by + P(5), "Cancella", P(14), FONT_SEMIBOLD, WHITE, 255);
	int sx = bx - P(80);
	gfx_rrect(sx, by, P(72), P(28), P(14), RGB(0, 122, 255), 255);
	text_center(sx + P(36), by + P(5), "Salva", P(14), FONT_SEMIBOLD, WHITE, 255);

	if (ui_now_ms() < saved_until) {
		const char *msg = "Salvato in Galleria";
		int tw = text_width(msg, P(15), FONT_SEMIBOLD) + P(32);
		gfx_rrect((G.w - tw) / 2, G.h / 2 - P(22), tw, P(44), P(22), BLACK, 170);
		text_center(G.w / 2, G.h / 2 - P(11), msg, P(15), FONT_SEMIBOLD, WHITE, 255);
	}
}

static void timer(void) { ui_redraw(); } /* toglie il messaggio "Salvato" */

static int in_bar(int x, int y)
{
	(void)x;
	return y >= bar_y() && y < bar_y() + P(84);
}

static void bar_tap(int tx, int ty)
{
	int y = bar_y(), x = P(10), w = G.w - P(20);
	int cs = P(30), gap = (w - P(20) - NPAL * cs) / (NPAL - 1);

	if (ty < y + P(44)) {
		int i = (tx - x - P(10) + gap / 2) / (cs + gap);
		if (i >= 0 && i < NPAL)
			color_i = i;
	} else if (tx > x + w - P(100)) {
		clear_canvas();
	} else if (tx > x + w - P(180)) {
		char path[160];
		if (photo_save(&canvas, path, sizeof(path)) == 0) {
			saved_until = ui_now_ms() + 1500;
			ui_timer(1600);
		}
	} else {
		int i = (tx - x - P(7)) / P(46);
		if (i >= 0 && i < 3)
			size_i = i;
	}
	ui_redraw();
}

static void event(const struct event *ev)
{
	if (!have_canvas)
		return;
	switch (ev->type) {
	case IN_TOUCH_DOWN:
		if (in_bar(ev->x, ev->y)) {
			bar_tap(ev->x, ev->y);
			last_x = -1;
			break;
		}
		if (ev->y < ui_status_h())
			break;
		stroke(ev->x, ev->y, ev->x, ev->y);
		last_x = ev->x;
		last_y = ev->y;
		ui_redraw();
		break;
	case IN_TOUCH_MOVE:
		if (last_x < 0)
			break;
		stroke(last_x, last_y, ev->x, ev->y);
		last_x = ev->x;
		last_y = ev->y;
		ui_redraw();
		break;
	case IN_TOUCH_UP:
		last_x = -1;
		break;
	default:
		break;
	}
}

static void icon(int x, int y, int s)
{
	float f = s / 60.0f;
	gfx_line(x + 14 * f, y + 40 * f, x + 22 * f, y + 30 * f, 6 * f, RGB(255, 59, 48), 255);
	gfx_line(x + 22 * f, y + 30 * f, x + 32 * f, y + 38 * f, 6 * f, RGB(0, 122, 255), 255);
	gfx_line(x + 32 * f, y + 38 * f, x + 44 * f, y + 20 * f, 6 * f, RGB(52, 199, 89), 255);
	gfx_circle(x + 46 * f, y + 16 * f, 5 * f, RGB(28, 28, 30), 255);
}

struct app app_paint = {
	.name = "Disegno",
	.icon_top = RGB(255, 255, 255), .icon_bottom = RGB(225, 228, 235),
	.icon = icon, .open = open_, .draw = draw, .event = event, .timer = timer, .light = 1,
};
