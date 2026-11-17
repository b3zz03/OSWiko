/* OSWiko - Bolle: tocca le bolle prima che volino via (gioco rilassante,
 * senza game over, pensato per i più piccoli). */
#include "ui.h"

#include <math.h>
#include <stdio.h>

#define MAX_B   14
#define MAX_POP 8
#define FRAME   33  /* ms: ~30 fotogrammi al secondo */

static const color_t colors[] = {
	RGB(255, 59, 48), RGB(255, 149, 0), RGB(255, 204, 0), RGB(52, 199, 89),
	RGB(90, 200, 250), RGB(0, 122, 255), RGB(175, 82, 222), RGB(255, 45, 85),
};

struct bubble { float x, y, r, vy, phase; color_t c; int alive; };
struct pop { float x, y, r; color_t c; int t; };

static struct bubble b[MAX_B];
static struct pop pops[MAX_POP];
static int score, best;
static long long last_spawn;

static void spawn(void)
{
	for (int i = 0; i < MAX_B; i++) {
		if (b[i].alive)
			continue;
		b[i].r = P(26 + ui_rand() % 22);
		b[i].x = b[i].r + ui_rand() % (unsigned)(G.w - 2 * (int)b[i].r);
		b[i].y = G.h + b[i].r;
		b[i].vy = P(1.6f + (ui_rand() % 100) / 50.0f + score * 0.02f);
		b[i].phase = (ui_rand() % 628) / 100.0f;
		b[i].c = colors[ui_rand() % 8];
		b[i].alive = 1;
		return;
	}
}

static void open_(void)
{
	for (int i = 0; i < MAX_B; i++)
		b[i].alive = 0;
	for (int i = 0; i < MAX_POP; i++)
		pops[i].t = 0;
	score = 0;
	last_spawn = 0;
	best = sys_load_int("bolle.record", 0);
	ui_timer(FRAME);
}

static void timer(void)
{
	long long now = ui_now_ms();
	if (now - last_spawn > 700) {
		spawn();
		last_spawn = now;
	}
	for (int i = 0; i < MAX_B; i++) {
		if (!b[i].alive)
			continue;
		b[i].y -= b[i].vy;
		b[i].phase += 0.06f;
		b[i].x += sinf(b[i].phase) * P(0.6f);
		if (b[i].y < -b[i].r)
			b[i].alive = 0;
	}
	for (int i = 0; i < MAX_POP; i++)
		if (pops[i].t > 0)
			pops[i].t--;
	ui_redraw();
	ui_timer(FRAME);
}

static void draw(void)
{
	char buf[32];

	gfx_vgradient(0, 0, G.w, G.h, RGB(90, 200, 250), RGB(0, 90, 200));
	for (int i = 0; i < MAX_B; i++) {
		if (!b[i].alive)
			continue;
		gfx_circle(b[i].x, b[i].y, b[i].r, b[i].c, 200);
		gfx_ring(b[i].x, b[i].y, b[i].r, P(2), WHITE, 120);
		gfx_circle(b[i].x - b[i].r * 0.35f, b[i].y - b[i].r * 0.35f,
			   b[i].r * 0.22f, WHITE, 170);
	}
	for (int i = 0; i < MAX_POP; i++) {
		if (pops[i].t <= 0)
			continue;
		float k = 1 - pops[i].t / 10.0f;
		gfx_ring(pops[i].x, pops[i].y, pops[i].r * (1 + k), P(3), pops[i].c,
			 (int)(255 * (1 - k)));
		for (int j = 0; j < 6; j++) {
			float a = j * (float)M_PI / 3;
			float d = pops[i].r * (1.1f + k * 0.9f);
			gfx_circle(pops[i].x + cosf(a) * d, pops[i].y + sinf(a) * d, P(4),
				   WHITE, (int)(255 * (1 - k)));
		}
	}
	snprintf(buf, sizeof(buf), "%d", score);
	text_center(G.w / 2, P(54), buf, P(56), FONT_SEMIBOLD, WHITE, 240);
	if (best > 0) {
		snprintf(buf, sizeof(buf), "Record: %d", best);
		text_center(G.w / 2, P(118), buf, P(15), FONT_REGULAR, WHITE, 200);
	}
}

static void event(const struct event *ev)
{
	if (ev->type != IN_TOUCH_DOWN)
		return;
	for (int i = MAX_B - 1; i >= 0; i--) {
		float dx = ev->x - b[i].x, dy = ev->y - b[i].y;
		if (!b[i].alive || dx * dx + dy * dy > (b[i].r + P(8)) * (b[i].r + P(8)))
			continue;
		b[i].alive = 0;
		score++;
		if (score > best) {
			best = score;
			sys_save_int("bolle.record", best);
		}
		for (int k = 0; k < MAX_POP; k++)
			if (pops[k].t <= 0) {
				pops[k] = (struct pop){ b[i].x, b[i].y, b[i].r, b[i].c, 10 };
				break;
			}
		ui_redraw();
		return;
	}
}

static void icon(int x, int y, int s)
{
	gfx_circle(x + s * 0.38f, y + s * 0.58f, s * 0.22f, WHITE, 120);
	gfx_ring(x + s * 0.38f, y + s * 0.58f, s * 0.22f, s * 0.04f, WHITE, 255);
	gfx_circle(x + s * 0.68f, y + s * 0.32f, s * 0.14f, WHITE, 120);
	gfx_ring(x + s * 0.68f, y + s * 0.32f, s * 0.14f, s * 0.035f, WHITE, 255);
	gfx_circle(x + s * 0.72f, y + s * 0.70f, s * 0.08f, WHITE, 200);
}

struct app app_bubbles = {
	.name = "Bolle",
	.icon_top = RGB(90, 200, 250), .icon_bottom = RGB(0, 110, 230),
	.icon = icon, .open = open_, .draw = draw, .event = event, .timer = timer,
};
