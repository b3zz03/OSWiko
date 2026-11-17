/* OSWiko - Torcia: lo schermo diventa una luce colorata */
#include "ui.h"

static const color_t lights[] = {
	RGB(255, 255, 255), RGB(255, 236, 190), RGB(255, 80, 80),
	RGB(90, 220, 120), RGB(80, 160, 255), RGB(220, 120, 255),
};
#define NL ((int)(sizeof(lights) / sizeof(lights[0])))

static int cur_light;
static int saved_bl = -1;

static void open_(void)
{
	saved_bl = sys_backlight_get();
	if (saved_bl >= 0)
		sys_backlight_set(100);
}

static void close_(void)
{
	if (saved_bl >= 0)
		sys_backlight_set(saved_bl);
}

static void draw(void)
{
	gfx_clear(lights[cur_light]);
	text_center(G.w / 2, G.h - P(70), "Tocca per cambiare colore", P(15),
		    FONT_REGULAR, BLACK, 110);
}

static void event(const struct event *ev)
{
	if (ev->type == IN_TOUCH_UP) {
		cur_light = (cur_light + 1) % NL;
		ui_redraw();
	}
}

static void icon(int x, int y, int s)
{
	float f = s / 60.0f;
	float beam[] = { x + 18 * f, y + 12 * f, x + 42 * f, y + 12 * f,
			 x + 36 * f, y + 26 * f, x + 24 * f, y + 26 * f };
	gfx_poly(beam, 4, WHITE, 255);
	gfx_rrect(x + 24 * f, y + 27 * f, 12 * f, 22 * f, 3 * f, WHITE, 255);
	gfx_circle(x + 30 * f, y + 35 * f, 2.2f * f, RGB(30, 30, 34), 255);
}

struct app app_torch = {
	.name = "Torcia",
	.icon_top = RGB(70, 70, 74), .icon_bottom = RGB(20, 20, 22),
	.icon = icon, .open = open_, .close = close_, .draw = draw,
	.event = event, .light = 1,
};
