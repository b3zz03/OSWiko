/* OSWiko - Galleria: griglia di miniature e visualizzatore a schermo intero */
#include "photos.h"
#include "ui.h"

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define COLS 3

static char names[PHOTO_MAX][64];
static struct image thumbs[PHOTO_MAX];
static int n_photos;
static int scroll, content_h;
static int touch_x0, touch_y0, scroll0, moved;

static int viewing = -1;          /* indice della foto aperta, -1 = griglia */
static struct image big;          /* foto aperta, alla risoluzione dello schermo */
static int big_for = -1;
static int confirm_delete;
static char pending_path[160];    /* richiesta da un'altra app (gallery_show) */

static void free_all(void)
{
	for (int i = 0; i < n_photos; i++)
		img_free(&thumbs[i]);
	img_free(&big);
	big_for = -1;
	n_photos = 0;
}

static void reload(void)
{
	free_all();
	n_photos = photos_list(names, PHOTO_MAX);
}

static void open_(void)
{
	reload();
	scroll = 0;
	viewing = -1;
	confirm_delete = 0;
	if (pending_path[0]) {
		const char *base = strrchr(pending_path, '/');
		base = base ? base + 1 : pending_path;
		for (int i = 0; i < n_photos; i++)
			if (!strcmp(names[i], base))
				viewing = i;
		pending_path[0] = 0;
	}
}

static void close_(void)
{
	free_all();
}

void gallery_show(const char *path)
{
	snprintf(pending_path, sizeof(pending_path), "%s", path);
	ui_open(&app_gallery);
}

/* ---------------- disegno ---------------- */

static int cell(void) { return (G.w - (COLS - 1) * P(2)) / COLS; }
static int grid_top(void) { return P(130); }

static void draw_grid(void)
{
	char buf[64];
	int cs = cell(), gap = P(2), top = grid_top();

	gfx_clear(WHITE);
	int rows = (n_photos + COLS - 1) / COLS;
	content_h = rows * (cs + gap);
	int max = content_h - (G.h - top - P(30));
	if (scroll > max) scroll = max;
	if (scroll < 0) scroll = 0;

	for (int i = 0; i < n_photos; i++) {
		int x = (i % COLS) * (cs + gap), y = top + (i / COLS) * (cs + gap) - scroll;
		if (y + cs < top - P(90) || y > G.h)
			continue;
		if (!thumbs[i].px)
			photo_thumb(names[i], &thumbs[i]); /* caricata solo quando serve */
		if (thumbs[i].px)
			gfx_image(&thumbs[i], x, y, cs, cs, 1, 0);
		else
			gfx_rect(x, y, cs, cs, RGB(230, 230, 235), 255);
	}
	/* intestazione sopra le foto che scorrono */
	gfx_rect(0, 0, G.w, top - P(4), WHITE, 245);
	text_draw(P(20), P(52), "Galleria", P(34), FONT_SEMIBOLD, BLACK, 255);
	snprintf(buf, sizeof(buf), n_photos == 1 ? "1 foto" : "%d foto", n_photos);
	text_draw(P(20), P(98), buf, P(15), FONT_REGULAR, RGB(142, 142, 147), 255);
	if (n_photos == 0)
		text_center(G.w / 2, G.h / 2 - P(20), "Nessuna foto. Scattane una con Fotocamera!",
			    P(15), FONT_REGULAR, RGB(142, 142, 147), 255);
}

static void trash_icon(int cx, int cy, color_t c)
{
	gfx_rrect(cx - P(8), cy - P(5), P(16), P(18), P(3), c, 255);
	gfx_rect(cx - P(11), cy - P(9), P(22), P(3), c, 255);
	gfx_rect(cx - P(4), cy - P(12), P(8), P(3), c, 255);
}

static void draw_view(void)
{
	char path[160], buf[64];

	gfx_clear(BLACK);
	if (big_for != viewing) {
		img_free(&big);
		photo_path(names[viewing], path, sizeof(path));
		img_load_file(&big, path, G.w, G.h);
		big_for = viewing;
	}
	if (big.px)
		gfx_image(&big, 0, 0, G.w, G.h, 0, 0);
	else
		text_center(G.w / 2, G.h / 2, "Impossibile aprire la foto", P(16), FONT_REGULAR, WHITE, 255);

	/* barre in alto e in basso, semitrasparenti */
	gfx_rect(0, 0, G.w, P(100), BLACK, 140);
	gfx_line(P(22), P(70), P(31), P(61), P(3), WHITE, 255);
	gfx_line(P(22), P(70), P(31), P(79), P(3), WHITE, 255);
	struct stat st;
	photo_path(names[viewing], path, sizeof(path));
	if (stat(path, &st) == 0) {
		struct tm tm;
		localtime_r(&st.st_mtime, &tm);
		strftime(buf, sizeof(buf), "%d/%m/%Y  %H:%M", &tm);
		text_center(G.w / 2, P(60), buf, P(15), FONT_SEMIBOLD, WHITE, 255);
	}
	snprintf(buf, sizeof(buf), "%d di %d", viewing + 1, n_photos);
	text_center(G.w / 2, P(80), buf, P(12), FONT_REGULAR, WHITE, 180);
	trash_icon(G.w - P(30), P(72), confirm_delete ? RGB(255, 69, 58) : WHITE);

	int by = G.h - P(100);
	if (confirm_delete) {
		gfx_rrect(P(40), by, G.w - P(80), P(48), P(24), RGB(255, 69, 58), 255);
		text_center(G.w / 2, by + P(13), "Tocca qui per eliminare", P(17), FONT_SEMIBOLD, WHITE, 255);
	} else if (chat_can_send()) {
		char lab[96];
		snprintf(lab, sizeof(lab), "Invia a %s", chat_contact());
		gfx_rrect(P(40), by, G.w - P(80), P(48), P(24), RGB(0, 122, 255), 255);
		text_center(G.w / 2, by + P(13), lab, P(17), FONT_SEMIBOLD, WHITE, 255);
	}
}

static void draw(void)
{
	/* barra di stato bianca sul nero del visualizzatore */
	app_gallery.light = !(viewing >= 0 && viewing < n_photos);
	if (viewing >= 0 && viewing < n_photos)
		draw_view();
	else
		draw_grid();
}

/* ---------------- input ---------------- */

static void view_tap(int x, int y)
{
	int by = G.h - P(100);
	char path[160];

	if (y < P(100) && x < P(70)) {
		viewing = -1;
		confirm_delete = 0;
	} else if (y < P(100) && x > G.w - P(64)) {
		confirm_delete = !confirm_delete;
	} else if (y >= by && y < by + P(48)) {
		if (confirm_delete) {
			photo_delete(names[viewing]);
			int was = viewing;
			reload();
			viewing = n_photos ? (was < n_photos ? was : n_photos - 1) : -1;
			confirm_delete = 0;
		} else if (chat_can_send()) {
			photo_path(names[viewing], path, sizeof(path));
			chat_send_photo(path);
			ui_open(&app_chat);
			return;
		}
	} else {
		confirm_delete = 0;
	}
	ui_redraw();
}

static void event(const struct event *ev)
{
	if (ev->type == IN_KEY && viewing >= 0) {
		if (ev->code == KEY_LEFT && viewing > 0) viewing--;
		else if (ev->code == KEY_RIGHT && viewing < n_photos - 1) viewing++;
		ui_redraw();
		return;
	}
	switch (ev->type) {
	case IN_TOUCH_DOWN:
		touch_x0 = ev->x;
		touch_y0 = ev->y;
		scroll0 = scroll;
		moved = 0;
		break;
	case IN_TOUCH_MOVE:
		if (abs(ev->y - touch_y0) > P(8) || abs(ev->x - touch_x0) > P(8))
			moved = 1;
		if (viewing < 0 && moved) {
			scroll = scroll0 - (ev->y - touch_y0);
			ui_redraw();
		}
		break;
	case IN_TOUCH_UP:
		if (viewing >= 0) {
			int dx = ev->x - touch_x0;
			if (moved && abs(dx) > P(50)) { /* swipe: foto successiva/precedente */
				if (dx < 0 && viewing < n_photos - 1) viewing++;
				else if (dx > 0 && viewing > 0) viewing--;
				confirm_delete = 0;
				ui_redraw();
			} else if (!moved) {
				view_tap(ev->x, ev->y);
			}
			break;
		}
		if (!moved && ev->y > grid_top()) {
			int cs = cell(), gap = P(2);
			int c = ev->x / (cs + gap), r = (ev->y - grid_top() + scroll) / (cs + gap);
			int i = r * COLS + c;
			if (c < COLS && i < n_photos) {
				viewing = i;
				ui_redraw();
			}
		}
		break;
	default:
		break;
	}
}

static void icon(int x, int y, int s)
{
	/* fiore a petali colorati */
	static const color_t pet[] = { RGB(255, 59, 48), RGB(255, 149, 0), RGB(255, 204, 0),
				       RGB(52, 199, 89), RGB(0, 199, 190), RGB(0, 122, 255),
				       RGB(88, 86, 214), RGB(255, 45, 85) };
	float cx = x + s / 2.0f, cy = y + s / 2.0f;
	for (int i = 0; i < 8; i++) {
		float a = i * 3.14159265f / 4;
		gfx_circle(cx + __builtin_cosf(a) * s * 0.19f, cy + __builtin_sinf(a) * s * 0.19f,
			   s * 0.13f, pet[i], 210);
	}
}

struct app app_gallery = {
	.name = "Galleria",
	.icon_top = RGB(255, 255, 255), .icon_bottom = RGB(235, 235, 240),
	.icon = icon, .open = open_, .close = close_, .draw = draw, .event = event,
	.light = 1,
};
