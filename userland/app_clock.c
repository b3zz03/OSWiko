/* OSWiko - Orologio: quadrante analogico + ora digitale */
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#define ORANGE RGB(255, 159, 10)

static void hand(float cx, float cy, float angle, float len, float back,
		 float thick, color_t c)
{
	float sx = sinf(angle), sy = -cosf(angle);
	gfx_line(cx - sx * back, cy - sy * back, cx + sx * len, cy + sy * len,
		 thick, c, 255);
}

static void face(float cx, float cy, float r, const struct tm *tm, int secs,
		 color_t bg, color_t fg)
{
	gfx_circle(cx, cy, r, bg, 255);
	for (int i = 0; i < 60; i++) {
		float a = i * (float)M_PI / 30;
		int major = i % 5 == 0;
		float r0 = r * (major ? 0.84f : 0.90f), r1 = r * 0.95f;
		if (!major && r < P(40))
			continue;
		gfx_line(cx + sinf(a) * r0, cy - cosf(a) * r0, cx + sinf(a) * r1,
			 cy - cosf(a) * r1, r * (major ? 0.025f : 0.010f) + 1, fg,
			 major ? 255 : 140);
	}
	float m = tm->tm_min + tm->tm_sec / 60.0f;
	float h = (tm->tm_hour % 12) + m / 60.0f;
	hand(cx, cy, h * (float)M_PI / 6, r * 0.5f, 0, r * 0.07f, fg);
	hand(cx, cy, m * (float)M_PI / 30, r * 0.78f, 0, r * 0.045f, fg);
	if (secs) {
		hand(cx, cy, tm->tm_sec * (float)M_PI / 30, r * 0.86f, r * 0.15f,
		     r * 0.018f + 1, ORANGE);
		gfx_circle(cx, cy, r * 0.045f, ORANGE, 255);
	} else {
		gfx_circle(cx, cy, r * 0.06f, fg, 255);
	}
}

static void icon(int x, int y, int s)
{
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	face(x + s / 2.0f, y + s / 2.0f, s * 0.40f, &tm, 0, WHITE, BLACK);
}

static void schedule(void)
{
	ui_timer(1000 - (int)(ui_now_ms() % 1000));
}

static void open_(void) { schedule(); }

static void timer(void)
{
	ui_redraw();
	schedule();
}

static void draw(void)
{
	static const char *giorni[] = { "Domenica", "Lunedì", "Martedì", "Mercoledì",
					"Giovedì", "Venerdì", "Sabato" };
	static const char *mesi[] = { "gennaio", "febbraio", "marzo", "aprile",
				      "maggio", "giugno", "luglio", "agosto",
				      "settembre", "ottobre", "novembre", "dicembre" };
	time_t t = time(NULL);
	struct tm tm;
	char buf[64];

	localtime_r(&t, &tm);
	gfx_clear(BLACK);
	text_draw(P(20), P(56), "Orologio", P(34), FONT_SEMIBOLD, WHITE, 255);

	float r = P(130);
	float cx = G.w / 2.0f, cy = P(120) + r;
	gfx_circle(cx, cy, r + P(6), RGB(40, 40, 44), 255);
	face(cx, cy, r, &tm, 1, RGB(28, 28, 30), WHITE);

	int y = (int)(cy + r) + P(36);
	snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
	text_center(G.w / 2, y, buf, P(64), FONT_LIGHT, WHITE, 255);
	snprintf(buf, sizeof(buf), "%s %d %s %d", giorni[tm.tm_wday], tm.tm_mday,
		 mesi[tm.tm_mon], tm.tm_year + 1900);
	text_center(G.w / 2, y + P(80), buf, P(18), FONT_REGULAR, RGB(152, 152, 157), 255);
}

struct app app_clock = {
	.name = "Orologio",
	.icon_top = RGB(40, 40, 44), .icon_bottom = RGB(10, 10, 12),
	.icon = icon, .open = open_, .draw = draw, .timer = timer,
};
