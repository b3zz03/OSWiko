/* OSWiko - Fotocamera.
 *
 * Usa la videocamera vera se il sistema ne ha una (/dev/video0, V4L2).
 * Altrimenti (emulatore) mostra una "fotocamera virtuale": una scena
 * animata disegnata al momento, così l'app si può provare lo stesso.
 */
#include "photos.h"
#include "ui.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define FRAME_MS 66 /* ~15 fotogrammi al secondo */

int  cam_open(int *w, int *h);
int  cam_grab(struct image *dst);
void cam_close(void);

static struct image frame;     /* ultimo fotogramma */
static struct image last;      /* miniatura dell'ultima foto */
static int real_cam;
static float t;
static int flash;              /* fotogrammi rimasti del lampo bianco */
static long long toast_until;
static char last_path[160];
static int pressed;

/* ---- fotocamera virtuale: una scena che si muove ---- */
static void virtual_scene(void)
{
	uint32_t *screen = G.back;
	G.back = frame.px; /* frame ha le dimensioni dello schermo */
	float w = G.w, h = G.h;
	gfx_vgradient(0, 0, G.w, (int)(h * 0.62f), RGB(110, 180, 255), RGB(210, 235, 255));
	gfx_circle(w * 0.75f, h * 0.18f, w * 0.09f, RGB(255, 236, 160), 255);
	for (int i = 0; i < 3; i++) { /* nuvole che scorrono */
		float cx = fmodf(t * (20 + i * 8) + i * 300, w + 300) - 150, cy = h * (0.12f + i * 0.09f);
		gfx_circle(cx, cy, w * 0.07f, WHITE, 230);
		gfx_circle(cx + w * 0.07f, cy + w * 0.02f, w * 0.06f, WHITE, 230);
		gfx_circle(cx - w * 0.07f, cy + w * 0.02f, w * 0.05f, WHITE, 230);
	}
	float hills[] = { 0, h * 0.62f, 0, h * 0.52f, w * 0.3f, h * 0.45f, w * 0.6f, h * 0.55f,
			  w, h * 0.47f, w, h * 0.62f };
	gfx_poly(hills, 6, RGB(110, 190, 120), 255);
	gfx_vgradient(0, (int)(h * 0.62f), G.w, G.h - (int)(h * 0.62f), RGB(100, 180, 90), RGB(60, 130, 60));
	for (int i = 0; i < 5; i++) { /* fiori */
		float fx = w * (0.1f + i * 0.2f), fy = h * (0.72f + (i % 2) * 0.08f);
		gfx_line(fx, fy, fx, fy + h * 0.06f, w * 0.01f, RGB(40, 120, 40), 255);
		for (int k = 0; k < 5; k++) {
			float a = k * 1.2566f + t * 0.5f;
			gfx_circle(fx + cosf(a) * w * 0.025f, fy + sinf(a) * w * 0.025f, w * 0.02f,
				   i % 2 ? RGB(255, 90, 140) : RGB(255, 210, 60), 255);
		}
		gfx_circle(fx, fy, w * 0.015f, RGB(255, 240, 200), 255);
	}
	/* pallone che rimbalza */
	float bx = w * 0.5f + sinf(t * 0.9f) * w * 0.3f;
	float by = h * 0.8f - fabsf(sinf(t * 2.2f)) * h * 0.25f;
	gfx_circle(bx, h * 0.83f, w * 0.06f * (0.6f + 0.4f * (by / (h * 0.8f))), BLACK, 40);
	gfx_circle(bx, by, w * 0.06f, RGB(255, 80, 60), 255);
	gfx_circle(bx - w * 0.02f, by - w * 0.02f, w * 0.018f, WHITE, 170);
	G.back = screen;
}

static void open_(void)
{
	int w, h;
	real_cam = cam_open(&w, &h) == 0;
	if (!real_cam) {
		w = G.w;
		h = G.h;
	}
	if (frame.w != w || frame.h != h) {
		img_free(&frame);
		img_alloc(&frame, w, h);
	}
	/* miniatura dell'ultima foto */
	char names[1][64];
	img_free(&last);
	if (photos_list(names, 1) == 1)
		photo_thumb(names[0], &last);
	flash = 0;
	ui_timer(FRAME_MS);
}

static void close_(void)
{
	if (real_cam)
		cam_close();
	img_free(&frame);
}

static void timer(void)
{
	t += FRAME_MS / 1000.0f;
	if (real_cam)
		cam_grab(&frame);
	else if (frame.px)
		virtual_scene();
	if (flash > 0)
		flash--;
	ui_redraw();
	ui_timer(FRAME_MS);
}

static void take_photo(void)
{
	if (!frame.px)
		return;
	if (photo_save(&frame, last_path, sizeof(last_path)) == 0) {
		const char *name = strrchr(last_path, '/') + 1;
		img_free(&last);
		photo_thumb(name, &last);
		flash = 4;
		toast_until = ui_now_ms() + 1500;
	}
}

static int shutter_y(void) { return G.h - P(110); }

static void draw(void)
{
	gfx_clear(BLACK);
	if (frame.px) {
		if (!real_cam && frame.w == G.w && frame.h == G.h)
			gfx_blit(&frame);
		else
			gfx_image(&frame, 0, 0, G.w, G.h, 1, 0);
	}
	/* barre semitrasparenti */
	gfx_rect(0, 0, G.w, P(96), BLACK, 110);
	gfx_rect(0, G.h - P(180), G.w, P(180), BLACK, 140);
	if (!real_cam) {
		const char *lab = "Fotocamera virtuale";
		int tw = text_width(lab, P(13), FONT_SEMIBOLD) + P(24);
		gfx_rrect((G.w - tw) / 2, P(56), tw, P(26), P(13), RGB(255, 204, 0), 230);
		text_center(G.w / 2, P(61), lab, P(13), FONT_SEMIBOLD, BLACK, 255);
	}
	text_center(G.w / 2, G.h - P(168), "FOTO", P(13), FONT_SEMIBOLD, RGB(255, 204, 0), 255);

	/* pulsante di scatto */
	float cx = G.w / 2.0f, cy = shutter_y();
	gfx_ring(cx, cy, P(36), P(4), WHITE, 255);
	gfx_circle(cx, cy, P(29), pressed ? RGB(200, 200, 200) : WHITE, 255);

	/* ultima foto */
	int ts = P(50), tx = P(34), ty = (int)cy - ts / 2;
	if (last.px)
		gfx_image(&last, tx, ty, ts, ts, 1, P(8));
	else
		gfx_rrect(tx, ty, ts, ts, P(8), RGB(60, 60, 64), 255);

	if (flash > 0)
		gfx_rect(0, 0, G.w, G.h, WHITE, flash * 60);
	if (ui_now_ms() < toast_until) {
		const char *msg = "Foto salvata in Galleria";
		int tw = text_width(msg, P(15), FONT_SEMIBOLD) + P(32);
		gfx_rrect((G.w - tw) / 2, G.h / 2 - P(22), tw, P(44), P(22), BLACK, 170);
		text_center(G.w / 2, G.h / 2 - P(11), msg, P(15), FONT_SEMIBOLD, WHITE, 255);
	}
}

static void event(const struct event *ev)
{
	float dx = ev->x - G.w / 2.0f, dy = ev->y - shutter_y();
	int on_shutter = dx * dx + dy * dy < P(44) * P(44);

	if (ev->type == IN_KEY && (ev->code == KEY_SPACE || ev->code == KEY_ENTER ||
				   ev->code == KEY_VOLUMEUP || ev->code == KEY_CAMERA)) {
		take_photo();
		return;
	}
	if (ev->type == IN_TOUCH_DOWN) {
		pressed = on_shutter;
	} else if (ev->type == IN_TOUCH_UP) {
		if (pressed && on_shutter)
			take_photo();
		else if (ev->x < P(100) && fabsf(dy) < P(40) && last.px && last_path[0])
			gallery_show(last_path);
		else if (ev->x < P(100) && fabsf(dy) < P(40) && last.px)
			ui_open(&app_gallery);
		pressed = 0;
	}
}

static void icon(int x, int y, int s)
{
	float f = s / 60.0f;
	gfx_rrect(x + 10 * f, y + 19 * f, 40 * f, 27 * f, 6 * f, RGB(40, 40, 44), 255);
	gfx_rrect(x + 22 * f, y + 14 * f, 16 * f, 8 * f, 3 * f, RGB(40, 40, 44), 255);
	gfx_circle(x + 30 * f, y + 32.5f * f, 9 * f, RGB(170, 170, 178), 255);
	gfx_circle(x + 30 * f, y + 32.5f * f, 6 * f, RGB(40, 40, 44), 255);
	gfx_circle(x + 43 * f, y + 24 * f, 1.8f * f, RGB(255, 204, 0), 255);
}

struct app app_camera = {
	.name = "Fotocamera",
	.icon_top = RGB(210, 210, 216), .icon_bottom = RGB(160, 160, 168),
	.icon = icon, .open = open_, .close = close_, .draw = draw, .event = event,
	.timer = timer,
};
