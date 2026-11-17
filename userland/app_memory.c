/* OSWiko - Memory: trova le coppie */
#include "ui.h"

#include <math.h>
#include <stdio.h>

#define COLS 4
#define ROWS 4
#define N (COLS * ROWS)

static const color_t colors[N / 2] = {
	RGB(255, 59, 48), RGB(255, 149, 0), RGB(255, 204, 0), RGB(52, 199, 89),
	RGB(0, 199, 190), RGB(0, 122, 255), RGB(175, 82, 222), RGB(255, 45, 85),
};

static int card[N];       /* simbolo 0..7 */
static int state[N];      /* 0 coperta, 1 scoperta, 2 indovinata */
static int first = -1, second = -1;
static int moves, found;
static int started;

static void new_game(void)
{
	for (int i = 0; i < N; i++)
		card[i] = i / 2, state[i] = 0;
	for (int i = N - 1; i > 0; i--) {
		int j = ui_rand() % (i + 1);
		int t = card[i]; card[i] = card[j]; card[j] = t;
	}
	first = second = -1;
	moves = found = 0;
	started = 1;
}

static void open_(void)
{
	if (!started || found == N / 2)
		new_game();
}

/* Disegna il simbolo k centrato in (cx,cy), raggio r. */
static void symbol(int k, float cx, float cy, float r, color_t c)
{
	float pts[20];
	switch (k) {
	case 0: gfx_circle(cx, cy, r, c, 255); break;
	case 1: gfx_rrect(cx - r * 0.85f, cy - r * 0.85f, r * 1.7f, r * 1.7f, r * 0.3f, c, 255); break;
	case 2: /* triangolo */
		pts[0] = cx; pts[1] = cy - r;
		pts[2] = cx + r * 0.95f; pts[3] = cy + r * 0.75f;
		pts[4] = cx - r * 0.95f; pts[5] = cy + r * 0.75f;
		gfx_poly(pts, 3, c, 255);
		break;
	case 3: /* rombo */
		pts[0] = cx; pts[1] = cy - r; pts[2] = cx + r * 0.8f; pts[3] = cy;
		pts[4] = cx; pts[5] = cy + r; pts[6] = cx - r * 0.8f; pts[7] = cy;
		gfx_poly(pts, 4, c, 255);
		break;
	case 4: /* stella */
		for (int i = 0; i < 10; i++) {
			float a = i * (float)M_PI / 5 - (float)M_PI / 2;
			float rr = i % 2 ? r * 0.45f : r;
			pts[2 * i] = cx + cosf(a) * rr;
			pts[2 * i + 1] = cy + sinf(a) * rr;
		}
		gfx_poly(pts, 10, c, 255);
		break;
	case 5: /* cuore */
		gfx_circle(cx - r * 0.45f, cy - r * 0.3f, r * 0.5f, c, 255);
		gfx_circle(cx + r * 0.45f, cy - r * 0.3f, r * 0.5f, c, 255);
		pts[0] = cx - r * 0.93f; pts[1] = cy - r * 0.15f;
		pts[2] = cx + r * 0.93f; pts[3] = cy - r * 0.15f;
		pts[4] = cx; pts[5] = cy + r * 0.85f;
		gfx_poly(pts, 3, c, 255);
		break;
	case 6: /* anello */
		gfx_ring(cx, cy, r, r * 0.38f, c, 255);
		break;
	case 7: /* croce */
		gfx_line(cx - r * 0.7f, cy - r * 0.7f, cx + r * 0.7f, cy + r * 0.7f, r * 0.45f, c, 255);
		gfx_line(cx + r * 0.7f, cy - r * 0.7f, cx - r * 0.7f, cy + r * 0.7f, r * 0.45f, c, 255);
		break;
	}
}

static void layout(int *x0, int *y0, int *cs, int *gap)
{
	*gap = P(10);
	*cs = (G.w - P(32) - (COLS - 1) * *gap) / COLS;
	*x0 = P(16);
	*y0 = P(150);
}

static void button_rect(int *x, int *y, int *w, int *h)
{
	*w = P(200); *h = P(50);
	*x = (G.w - *w) / 2;
	*y = G.h - P(110);
}

static void draw(void)
{
	int x0, y0, cs, gap, bx, by, bw, bh;
	char buf[64];

	layout(&x0, &y0, &cs, &gap);
	gfx_vgradient(0, 0, G.w, G.h, RGB(88, 86, 214), RGB(175, 82, 222));
	text_draw(P(20), P(56), "Memory", P(34), FONT_SEMIBOLD, WHITE, 255);
	snprintf(buf, sizeof(buf), "Mosse: %d    Coppie: %d/%d", moves, found, N / 2);
	text_draw(P(20), P(102), buf, P(16), FONT_REGULAR, WHITE, 220);

	for (int i = 0; i < N; i++) {
		int x = x0 + (i % COLS) * (cs + gap), y = y0 + (i / COLS) * (cs + gap);
		gfx_shadow(x, y, cs, cs, P(14), P(4), 50);
		if (state[i] == 0) {
			gfx_rrect_grad(x, y, cs, cs, P(14), RGB(255, 255, 255), RGB(220, 222, 240), 255);
			gfx_circle(x + cs / 2.0f, y + cs / 2.0f, cs * 0.16f, RGB(120, 110, 220), 120);
		} else {
			gfx_rrect(x, y, cs, cs, P(14), WHITE, state[i] == 2 ? 150 : 255);
			symbol(card[i], x + cs / 2.0f, y + cs / 2.0f, cs * 0.3f, colors[card[i]]);
		}
	}

	if (found == N / 2) {
		gfx_rect(0, 0, G.w, G.h, BLACK, 110);
		int pw = P(280), ph = P(200), px = (G.w - pw) / 2, py = P(220);
		gfx_rrect(px, py, pw, ph, P(28), WHITE, 250);
		text_center(G.w / 2, py + P(30), "Bravo!", P(40), FONT_SEMIBOLD, RGB(88, 86, 214), 255);
		snprintf(buf, sizeof(buf), "Hai vinto in %d mosse", moves);
		text_center(G.w / 2, py + P(90), buf, P(18), FONT_REGULAR, RGB(60, 60, 67), 255);
		text_center(G.w / 2, py + P(140), "Tocca per giocare ancora", P(15),
			    FONT_SEMIBOLD, RGB(0, 122, 255), 255);
		return;
	}
	button_rect(&bx, &by, &bw, &bh);
	gfx_rrect(bx, by, bw, bh, bh / 2, WHITE, 60);
	text_center(G.w / 2, by + P(14), "Nuova partita", P(18), FONT_SEMIBOLD, WHITE, 255);
}

static void timer(void)
{
	/* copri le due carte sbagliate */
	if (first >= 0 && second >= 0) {
		state[first] = state[second] = 0;
		first = second = -1;
		ui_redraw();
	}
}

static void event(const struct event *ev)
{
	int x0, y0, cs, gap, bx, by, bw, bh;

	if (ev->type != IN_TOUCH_UP)
		return;
	if (found == N / 2) {
		new_game();
		ui_redraw();
		return;
	}
	button_rect(&bx, &by, &bw, &bh);
	if (ev->x >= bx && ev->x < bx + bw && ev->y >= by && ev->y < by + bh) {
		new_game();
		ui_timer(0);
		ui_redraw();
		return;
	}
	layout(&x0, &y0, &cs, &gap);
	int c = (ev->x - x0) / (cs + gap), r = (ev->y - y0) / (cs + gap);
	if (ev->x < x0 || ev->y < y0 || c >= COLS || r >= ROWS)
		return;
	int i = r * COLS + c;
	if (state[i] != 0)
		return;
	if (second >= 0)
		timer(); /* tocco veloce: copri subito le precedenti */
	state[i] = 1;
	if (first < 0) {
		first = i;
	} else {
		second = i;
		moves++;
		if (card[first] == card[second]) {
			state[first] = state[second] = 2;
			found++;
			first = second = -1;
		} else {
			ui_timer(800);
		}
	}
	ui_redraw();
}

static void icon(int x, int y, int s)
{
	float cs = s * 0.3f, g = s * 0.06f, ox = x + (s - 2 * cs - g) / 2, oy = y + (s - 2 * cs - g) / 2;
	for (int i = 0; i < 4; i++) {
		float cx = ox + (i % 2) * (cs + g), cy = oy + (i / 2) * (cs + g);
		gfx_rrect(cx, cy, cs, cs, cs * 0.25f, WHITE, i == 1 ? 255 : 200);
	}
	symbol(5, ox + cs + g + cs / 2, oy + cs / 2, cs * 0.3f, RGB(255, 45, 85));
}

struct app app_memory = {
	.name = "Memory",
	.icon_top = RGB(120, 110, 240), .icon_bottom = RGB(175, 82, 222),
	.icon = icon, .open = open_, .draw = draw, .event = event, .timer = timer,
};
