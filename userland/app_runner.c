/* OSWiko - Volpe in Fuga: corsa infinita su 3 corsie in un bosco.
 *
 * Scorri a sinistra/destra per cambiare corsia, in su per saltare i
 * tronchi, in giù per scivolare sotto le staccionate. Evita i carretti,
 * raccogli le gemme. Grafica 3D in prospettiva calcolata a mano.
 */
#include "ui.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_MS 33
#define MAX_OBJ  96

#define CAM_H    1.5f   /* altezza della telecamera */
#define PLAYER_Z 2.6f   /* distanza giocatore-telecamera */
#define Z_NEAR   0.5f
#define Z_FAR    42.0f
#define ROAD_W   1.75f  /* metà larghezza strada */

enum { LOG, BAR, CART, GEM };

struct obj { int type; float x, w, depth, y; int taken; };

static struct obj objs[MAX_OBJ];
static int n_objs;

static float dist, speed, xp, yj, vj, slide, anim;
static int lane, started, crashed, gems, best;
static long long crash_at, last_ms;
static float next_row;
static unsigned seed = 1;

/* gesture */
static int sw_x0, sw_y0, sw_active;

static unsigned rnd(void)
{
	seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
	return seed;
}

static float frand(void) { return (rnd() % 10000) / 10000.0f; }

/* Il campo y di obj contiene la posizione z nel mondo. */
static void add(int type, float lane_x, float w, float depth, float z)
{
	if (n_objs < MAX_OBJ)
		objs[n_objs++] = (struct obj){ type, lane_x, w, depth, z, 0 };
}

static void spawn_row(float w)
{
	float diff = dist / 700;
	int kind[3], n = 0;

	if (diff > 1)
		diff = 1;
	for (int l = 0; l < 3; l++) {
		kind[l] = -1;
		if (frand() < 0.38f + 0.25f * diff) {
			float r = frand();
			kind[l] = r < 0.4f ? LOG : r < 0.68f ? BAR : CART;
			n++;
		}
	}
	/* almeno una corsia senza carretto, e mai una fila vuota */
	if (kind[0] == CART && kind[1] == CART && kind[2] == CART)
		kind[rnd() % 3] = LOG;
	if (n == 0)
		kind[rnd() % 3] = rnd() % 2 ? LOG : CART;

	for (int l = 0; l < 3; l++) {
		float x = l - 1;
		if (kind[l] == LOG)
			add(LOG, x, 0.92f, 0.5f, w);
		else if (kind[l] == BAR)
			add(BAR, x, 0.95f, 0.25f, w);
		else if (kind[l] == CART)
			add(CART, x, 0.9f, 2.4f, w);
	}
	/* gemme in una corsia libera, prima della fila */
	int free_l[3], nf = 0;
	for (int l = 0; l < 3; l++)
		if (kind[l] < 0)
			free_l[nf++] = l;
	if (nf && frand() < 0.7f) {
		int l = free_l[rnd() % nf];
		for (int i = 0; i < 5; i++)
			add(GEM, l - 1, 0.3f, 0.3f, w - 6 + i * 1.2f);
	}
}

#define WZ(o) ((o)->y)

static void reset(void)
{
	n_objs = 0;
	dist = 0;
	speed = 8;
	lane = 0;
	xp = 0; yj = 0; vj = 0; slide = 0;
	gems = 0;
	crashed = 0;
	next_row = 18;
	seed = (unsigned)ui_now_ms() | 1;
}

static void crash(void)
{
	crashed = 1;
	crash_at = ui_now_ms();
	int score = (int)dist + gems * 5;
	if (score > best) {
		best = score;
		sys_save_int("volpe.record", best);
	}
}

static void update(float dt)
{
	dist += speed * dt;
	speed = 8 + dist / 110;
	if (speed > 17)
		speed = 17;
	anim += dt * speed;

	float dx = lane - xp, mv = 9 * dt;
	xp += dx > mv ? mv : dx < -mv ? -mv : dx;

	vj -= 24 * dt;
	yj += vj * dt;
	if (yj <= 0) {
		yj = 0;
		vj = 0;
	}
	if (slide > 0)
		slide -= dt;

	while (next_row < dist + Z_FAR) {
		spawn_row(next_row);
		float gap = speed * 0.85f;
		next_row += (gap < 8 ? 8 : gap) + frand() * 5;
	}

	int k = 0;
	for (int i = 0; i < n_objs; i++) {
		struct obj *o = &objs[i];
		if (WZ(o) + o->depth < dist - 3)
			continue; /* superato: elimina */
		objs[k++] = *o;
	}
	n_objs = k;

	for (int i = 0; i < n_objs && !crashed; i++) {
		struct obj *o = &objs[i];
		if (fabsf(xp - o->x) > 0.55f)
			continue;
		if (WZ(o) > dist + 0.3f || WZ(o) + o->depth < dist - 0.3f)
			continue;
		switch (o->type) {
		case LOG:  if (yj < 0.42f) crash(); break;
		case BAR:  if (slide <= 0) crash(); break;
		case CART: crash(); break;
		case GEM:
			if (!o->taken && yj < 1.0f) {
				o->taken = 1;
				gems++;
			}
			break;
		}
	}
}

static void act(int dir) /* 0 sx, 1 dx, 2 su, 3 giù */
{
	if (!started || crashed)
		return;
	if (dir == 0 && lane > -1)
		lane--;
	else if (dir == 1 && lane < 1)
		lane++;
	else if (dir == 2 && yj == 0) {
		vj = 8.2f;
		slide = 0;
	} else if (dir == 3) {
		if (yj > 0)
			vj = -16; /* scendi subito */
		slide = 0.75f;
	}
}

static void open_(void)
{
	reset();
	started = 0;
	best = sys_load_int("volpe.record", 0);
	last_ms = ui_now_ms();
	ui_timer(FRAME_MS);
}

static void timer(void)
{
	long long now = ui_now_ms();
	float dt = (now - last_ms) / 1000.0f;
	last_ms = now;
	if (dt > 0.05f)
		dt = 0.05f;
	if (started && !crashed) {
		update(dt / 2);
		if (!crashed)
			update(dt / 2);
	}
	ui_redraw();
	ui_timer(FRAME_MS);
}

/* ---------------- disegno 3D ---------------- */

static float F, HY, CX, camx;

static float psx(float x, float z) { return CX + (x - camx) * F / z; }
static float psy(float y, float z) { return HY + (CAM_H - y) * F / z; }

static void quad(float x0, float y0, float z0, float x1, float y1, float z1,
		 float x2, float y2, float z2, float x3, float y3, float z3,
		 color_t c, int a)
{
	float p[8] = { psx(x0, z0), psy(y0, z0), psx(x1, z1), psy(y1, z1),
		       psx(x2, z2), psy(y2, z2), psx(x3, z3), psy(y3, z3) };
	gfx_poly(p, 4, c, a);
}

/* Parallelepipedo: x centro, w larghezza, y0 base, h altezza, zn/zf profondità */
static void box(float x, float w, float y0, float h, float zn, float zf,
		color_t front, color_t top, color_t side)
{
	float l = x - w / 2, r = x + w / 2, t = y0 + h;
	if (zf <= Z_NEAR)
		return;
	if (zn < Z_NEAR)
		zn = Z_NEAR;
	if (l > camx)
		quad(l, y0, zn, l, t, zn, l, t, zf, l, y0, zf, side, 255);
	if (r < camx)
		quad(r, y0, zn, r, t, zn, r, t, zf, r, y0, zf, side, 255);
	if (t < CAM_H)
		quad(l, t, zn, r, t, zn, r, t, zf, l, t, zf, top, 255);
	quad(l, y0, zn, r, y0, zn, r, t, zn, l, t, zn, front, 255);
}

static void draw_tree(float x, float z, unsigned h)
{
	float s = F / z;
	float bx = psx(x, z), by = psy(0, z);
	float tw = 0.16f * s, th = 0.9f * s;
	color_t leaf = (h & 1) ? RGB(46, 140, 70) : RGB(60, 160, 60);
	gfx_rect((int)(bx - tw / 2), (int)(by - th), (int)tw + 1, (int)th, RGB(110, 70, 40), 255);
	gfx_circle(bx, by - th - 0.25f * s, 0.62f * s, gfx_mix(leaf, BLACK, 25, 100), 255);
	if (z > 22)
		return; /* da lontano basta un cerchio */
	gfx_circle(bx - 0.1f * s, by - th - 0.45f * s, 0.5f * s, leaf, 255);
	gfx_circle(bx - 0.18f * s, by - th - 0.6f * s, 0.2f * s, WHITE, 40);
}

static void draw_obj(struct obj *o)
{
	float zn = WZ(o) - dist + PLAYER_Z, zf = zn + o->depth;
	float x = o->x;

	switch (o->type) {
	case LOG: {
		box(x, o->w, 0, 0.42f, zn, zf, RGB(150, 95, 50), RGB(175, 115, 65), RGB(120, 75, 40));
		if (zn > Z_NEAR) {
			float s = F / zn, ex = psx(x - o->w / 2 + 0.12f, zn), ey = psy(0.21f, zn);
			gfx_circle(ex, ey, 0.17f * s, RGB(225, 185, 130), 255);
			gfx_ring(ex, ey, 0.11f * s, 0.025f * s + 1, RGB(160, 110, 60), 255);
		}
		break;
	}
	case BAR: {
		float pw = 0.1f;
		box(x - o->w / 2 + pw / 2, pw, 0, 1.25f, zn, zf, RGB(240, 240, 235), RGB(255, 255, 255), RGB(200, 200, 195));
		box(x + o->w / 2 - pw / 2, pw, 0, 1.25f, zn, zf, RGB(240, 240, 235), RGB(255, 255, 255), RGB(200, 200, 195));
		box(x, o->w + 0.1f, 0.78f, 0.34f, zn, zf, RGB(230, 60, 50), RGB(250, 90, 80), RGB(190, 45, 40));
		if (zn > Z_NEAR) /* strisce bianche */
			for (int i = 0; i < 3; i++) {
				float bx = x - 0.32f + i * 0.32f;
				quad(bx - 0.06f, 0.78f, zn, bx + 0.06f, 0.78f, zn, bx + 0.12f, 1.12f, zn,
				     bx, 1.12f, zn, WHITE, 230);
			}
		break;
	}
	case CART: {
		box(x, o->w, 0.25f, 1.15f, zn, zf, RGB(200, 60, 50), RGB(240, 200, 90), RGB(160, 45, 40));
		/* fieno sopra */
		box(x, o->w - 0.1f, 1.4f, 0.25f, zn + 0.1f, zf - 0.1f, RGB(235, 200, 90), RGB(250, 220, 120), RGB(210, 175, 70));
		if (zn > Z_NEAR) {
			float s = F / zn;
			for (int i = 0; i < 2; i++) {
				float wx = psx(x + (i ? 0.33f : -0.33f), zn), wy = psy(0.22f, zn);
				gfx_circle(wx, wy, 0.22f * s, RGB(50, 40, 40), 255);
				gfx_circle(wx, wy, 0.08f * s, RGB(180, 180, 180), 255);
			}
			/* assi di legno */
			for (int i = 1; i < 3; i++)
				gfx_rect((int)psx(x - o->w / 2, zn), (int)psy(0.25f + i * 0.38f, zn),
					 (int)(o->w * s), (int)(0.03f * s) + 1, RGB(150, 40, 35), 255);
		}
		break;
	}
	case GEM: {
		if (o->taken || zn < Z_NEAR)
			break;
		float s = F / zn, cx = psx(x, zn);
		float cy = psy(0.55f + 0.08f * sinf(anim * 0.6f + WZ(o)), zn);
		float hw = 0.2f * s * (0.6f + 0.4f * fabsf(sinf(anim * 0.4f + WZ(o))));
		float p[8] = { cx, cy - 0.24f * s, cx + hw, cy, cx, cy + 0.24f * s, cx - hw, cy };
		gfx_poly(p, 4, RGB(60, 210, 255), 255);
		float q[8] = { cx, cy - 0.24f * s, cx + hw * 0.5f, cy - 0.04f * s, cx, cy, cx - hw * 0.5f, cy - 0.04f * s };
		gfx_poly(q, 4, WHITE, 170);
		break;
	}
	}
}

static void draw_fox(void)
{
	float z = PLAYER_Z, s = F / z;
	float cx = psx(xp, z), gy = psy(0, z);
	float y = yj * s;
	color_t orange = RGB(245, 130, 40), dark = RGB(60, 35, 30);
	float run = sinf(anim * 2.2f);

	/* ombra */
	float shw = 0.55f * s * (1 - yj * 0.3f);
	gfx_rrect((int)(cx - shw / 2), (int)(gy - 0.06f * s), (int)shw, (int)(0.12f * s),
		  (int)(0.06f * s), BLACK, 70);

	if (slide > 0) {
		/* scivolata: corpo basso e allungato */
		gfx_circle(cx + 0.28f * s, gy - y - 0.2f * s, 0.14f * s, orange, 255);
		gfx_circle(cx + 0.36f * s, gy - y - 0.25f * s, 0.07f * s, WHITE, 255);
		gfx_rrect((int)(cx - 0.32f * s), (int)(gy - y - 0.36f * s), (int)(0.64f * s),
			  (int)(0.32f * s), (int)(0.16f * s), orange, 255);
		gfx_circle(cx, gy - y - 0.5f * s, 0.19f * s, orange, 255);
		float e1[6] = { cx - 0.17f * s, gy - y - 0.55f * s, cx - 0.07f * s, gy - y - 0.62f * s, cx - 0.2f * s, gy - y - 0.78f * s };
		float e2[6] = { cx + 0.17f * s, gy - y - 0.55f * s, cx + 0.07f * s, gy - y - 0.62f * s, cx + 0.2f * s, gy - y - 0.78f * s };
		gfx_poly(e1, 3, orange, 255);
		gfx_poly(e2, 3, orange, 255);
		return;
	}
	/* zampe */
	float lo = yj > 0 ? 0 : run * 0.06f * s;
	gfx_rrect((int)(cx - 0.17f * s), (int)(gy - y - 0.22f * s + lo), (int)(0.11f * s),
		  (int)(0.22f * s), (int)(0.05f * s), dark, 255);
	gfx_rrect((int)(cx + 0.06f * s), (int)(gy - y - 0.22f * s - lo), (int)(0.11f * s),
		  (int)(0.22f * s), (int)(0.05f * s), dark, 255);
	/* corpo */
	gfx_circle(cx, gy - y - 0.4f * s, 0.27f * s, orange, 255);
	/* coda che ondeggia */
	float tx = cx + run * 0.1f * s, ty = gy - y - 0.32f * s;
	gfx_circle(tx, ty, 0.17f * s, orange, 255);
	gfx_circle(tx + run * 0.04f * s, ty + 0.08f * s, 0.09f * s, WHITE, 255);
	/* testa e orecchie */
	float hy = gy - y - 0.78f * s;
	gfx_circle(cx, hy, 0.21f * s, orange, 255);
	float e1[6] = { cx - 0.19f * s, hy - 0.04f * s, cx - 0.06f * s, hy - 0.14f * s, cx - 0.2f * s, hy - 0.36f * s };
	float e2[6] = { cx + 0.19f * s, hy - 0.04f * s, cx + 0.06f * s, hy - 0.14f * s, cx + 0.2f * s, hy - 0.36f * s };
	gfx_poly(e1, 3, orange, 255);
	gfx_poly(e2, 3, orange, 255);
	float i1[6] = { cx - 0.16f * s, hy - 0.1f * s, cx - 0.1f * s, hy - 0.15f * s, cx - 0.18f * s, hy - 0.28f * s };
	float i2[6] = { cx + 0.16f * s, hy - 0.1f * s, cx + 0.1f * s, hy - 0.15f * s, cx + 0.18f * s, hy - 0.28f * s };
	gfx_poly(i1, 3, dark, 255);
	gfx_poly(i2, 3, dark, 255);
}

/* Riempie i pixel [xa, xb) di una riga, con anti-aliasing agli estremi. */
static void span(uint32_t *row, float xa, float xb, color_t c)
{
	if (xa < 0) xa = 0;
	if (xb > G.w) xb = G.w;
	if (xb <= xa)
		return;
	int ia = (int)xa, ib = (int)xb;
	if (ia == ib) {
		row[ia] = gfx_mix(row[ia], c, (int)((xb - xa) * 255), 255);
		return;
	}
	row[ia] = gfx_mix(row[ia], c, (int)((ia + 1 - xa) * 255), 255);
	for (int x = ia + 1; x < ib; x++)
		row[x] = c;
	if (ib < G.w)
		row[ib] = gfx_mix(row[ib], c, (int)((xb - ib) * 255), 255);
}

struct sprite { float z; int kind; int idx; float x; unsigned h; };

static int cmp_sprite(const void *a, const void *b)
{
	float d = ((const struct sprite *)b)->z - ((const struct sprite *)a)->z;
	return d > 0 ? 1 : d < 0 ? -1 : 0;
}

static void draw(void)
{
	char buf[48];
	struct sprite sp[MAX_OBJ + 64];
	int ns = 0;

	F = G.w * 0.72f;
	HY = G.h * 0.38f;
	CX = G.w / 2.0f;
	camx = xp * 0.6f;

	/* cielo, sole e colline sono fissi: disegnati una volta e poi copiati */
	static struct image sky;
	static int sky_ok;
	if (!sky_ok && img_alloc(&sky, G.w, G.h) == 0) {
		gfx_vgradient(0, 0, G.w, (int)HY + 1, RGB(90, 170, 255), RGB(200, 235, 255));
		gfx_circle(G.w * 0.32f, HY * 0.55f, P(34), RGB(255, 240, 180), 255);
		gfx_circle(G.w * 0.32f, HY * 0.55f, P(46), RGB(255, 240, 180), 60);
		float hills[] = { 0, HY, 0, HY - P(40), G.w * 0.2f, HY - P(70), G.w * 0.45f, HY - P(30),
				  G.w * 0.7f, HY - P(80), (float)G.w, HY - P(35), (float)G.w, HY };
		gfx_poly(hills, 7, RGB(120, 190, 140), 255);
		img_capture(&sky);
		sky_ok = 1;
	}
	if (sky_ok)
		memcpy(G.back, sky.px, (size_t)G.w * ((int)HY + 1) * 4);

	/* terreno disegnato riga per riga (tecnica dei giochi di corse "a scanline"):
	 * per ogni riga sotto l'orizzonte si calcola la distanza z e si riempie
	 * con erba, bordo, strada e strisce. Un solo passaggio, niente poligoni. */
	for (int y = (int)HY + 1; y < G.h; y++) {
		float z = CAM_H * F / (y + 0.5f - HY);
		if (z > Z_FAR + 4)
			z = Z_FAR + 4;
		int alt = ((int)floorf(z + dist - PLAYER_Z)) & 1;
		float k = F / z, c = CX - camx * k;
		uint32_t *row = G.back + (size_t)y * G.w;
		color_t grass = alt ? RGB(96, 178, 84) : RGB(84, 162, 74);
		for (int x = 0; x < G.w; x++)
			row[x] = grass;
		span(row, c - (ROAD_W + 0.15f) * k, c + (ROAD_W + 0.15f) * k, RGB(150, 120, 90));
		span(row, c - ROAD_W * k, c + ROAD_W * k, alt ? RGB(214, 186, 140) : RGB(204, 175, 128));
		if (alt)
			for (int l = -1; l <= 1; l += 2)
				span(row, c + (l * 0.5f - 0.03f) * k, c + (l * 0.5f + 0.03f) * k,
				     RGB(240, 225, 190));
	}

	/* alberi ai lati + oggetti, ordinati dal più lontano */
	for (float w = floorf(dist / 2.5f) * 2.5f; w < dist + Z_FAR; w += 2.5f) {
		float z = w - dist + PLAYER_Z;
		if (z <= Z_NEAR + 0.3f)
			continue;
		unsigned h = (unsigned)(w * 4) * 2654435761u;
		sp[ns++] = (struct sprite){ z, 0, 0, -(2.4f + (h >> 28) / 12.0f), h };
		sp[ns++] = (struct sprite){ z + 1.2f, 0, 0, 2.4f + ((h >> 20) & 15) / 12.0f, h >> 8 };
	}
	for (int i = 0; i < n_objs && ns < (int)(sizeof(sp) / sizeof(sp[0])) - 1; i++)
		sp[ns++] = (struct sprite){ WZ(&objs[i]) - dist + PLAYER_Z, 1, i, 0, 0 };
	sp[ns++] = (struct sprite){ PLAYER_Z - 0.01f, 2, 0, 0, 0 };
	qsort(sp, ns, sizeof(sp[0]), cmp_sprite);
	for (int i = 0; i < ns; i++) {
		if (sp[i].kind == 0)
			draw_tree(sp[i].x, sp[i].z, sp[i].h);
		else if (sp[i].kind == 1)
			draw_obj(&objs[sp[i].idx]);
		else if (!crashed || (ui_now_ms() / 150) % 2)
			draw_fox();
	}

	/* punteggio */
	snprintf(buf, sizeof(buf), "%d", (int)dist + gems * 5);
	gfx_rrect(P(14), P(50), text_width(buf, P(26), FONT_SEMIBOLD) + P(28), P(40), P(20), BLACK, 70);
	text_draw(P(28), P(56), buf, P(26), FONT_SEMIBOLD, WHITE, 255);
	snprintf(buf, sizeof(buf), "%d", gems);
	int gw = text_width(buf, P(22), FONT_SEMIBOLD);
	gfx_rrect(G.w - P(14) - gw - P(48), P(50), gw + P(48), P(40), P(20), BLACK, 70);
	float gx = G.w - P(14) - gw - P(30), gyc = P(70);
	float gp[8] = { gx, gyc - P(10), gx + P(8), gyc, gx, gyc + P(10), gx - P(8), gyc };
	gfx_poly(gp, 4, RGB(60, 210, 255), 255);
	text_draw(G.w - P(28) - gw, P(59), buf, P(22), FONT_SEMIBOLD, WHITE, 255);

	if (!started) {
		gfx_rect(0, 0, G.w, G.h, BLACK, 90);
		text_center(G.w / 2, P(180), "Volpe in Fuga", P(42), FONT_SEMIBOLD, WHITE, 255);
		text_center(G.w / 2, P(250), "← → scorri per cambiare corsia", P(16), FONT_REGULAR, WHITE, 230);
		text_center(G.w / 2, P(276), "↑ scorri in su per saltare i tronchi", P(16), FONT_REGULAR, WHITE, 230);
		text_center(G.w / 2, P(302), "↓ scorri in giù per passare sotto", P(16), FONT_REGULAR, WHITE, 230);
		text_center(G.w / 2, P(370), "Tocca per iniziare", P(20), FONT_SEMIBOLD, RGB(255, 200, 80), 255);
		if (best)
			text_center(G.w / 2, P(410), (snprintf(buf, sizeof(buf), "Record: %d", best), buf),
				    P(15), FONT_REGULAR, WHITE, 200);
	} else if (crashed) {
		int pw = P(280), ph = P(230), px = (G.w - pw) / 2, py = P(200);
		gfx_shadow(px, py, pw, ph, P(28), P(10), 80);
		gfx_rrect(px, py, pw, ph, P(28), WHITE, 250);
		text_center(G.w / 2, py + P(24), "Ops!", P(38), FONT_SEMIBOLD, RGB(245, 130, 40), 255);
		snprintf(buf, sizeof(buf), "Punti: %d", (int)dist + gems * 5);
		text_center(G.w / 2, py + P(84), buf, P(22), FONT_SEMIBOLD, RGB(40, 40, 45), 255);
		snprintf(buf, sizeof(buf), "Gemme: %d   •   Record: %d", gems, best);
		text_center(G.w / 2, py + P(120), buf, P(15), FONT_REGULAR, RGB(110, 110, 118), 255);
		if (ui_now_ms() - crash_at > 700)
			text_center(G.w / 2, py + P(172), "Tocca per riprovare", P(17), FONT_SEMIBOLD,
				    RGB(0, 122, 255), 255);
	}
}

static void event(const struct event *ev)
{
	if (ev->type == IN_KEY) {
		switch (ev->code) {
		case KEY_LEFT: case KEY_A: act(0); break;
		case KEY_RIGHT: case KEY_D: act(1); break;
		case KEY_UP: case KEY_W: case KEY_SPACE: act(2); break;
		case KEY_DOWN: case KEY_S: act(3); break;
		}
		if (!started || (crashed && ui_now_ms() - crash_at > 700)) {
			reset();
			started = 1;
		}
		return;
	}
	if (ev->type == IN_TOUCH_DOWN) {
		sw_x0 = ev->x;
		sw_y0 = ev->y;
		sw_active = 1;
	} else if (ev->type == IN_TOUCH_MOVE && sw_active) {
		int dx = ev->x - sw_x0, dy = ev->y - sw_y0;
		if (abs(dx) > P(28) || abs(dy) > P(28)) {
			if (abs(dx) > abs(dy))
				act(dx < 0 ? 0 : 1);
			else
				act(dy < 0 ? 2 : 3);
			sw_active = 0; /* uno swipe = un'azione */
		}
	} else if (ev->type == IN_TOUCH_UP) {
		if (sw_active && (!started || (crashed && ui_now_ms() - crash_at > 700))) {
			reset();
			started = 1;
		}
		sw_active = 0;
	}
}

static void icon(int x, int y, int s)
{
	float f = s / 60.0f;
	float road[8] = { x + 24 * f, y + 30 * f, x + 36 * f, y + 30 * f, x + 54 * f, y + 60 * f, x + 6 * f, y + 60 * f };
	gfx_rect(x, y + 30 * f, s, 30 * f, RGB(90, 170, 80), 255);
	gfx_poly(road, 4, RGB(214, 186, 140), 255);
	gfx_circle(x + 30 * f, y + 40 * f, 8 * f, RGB(245, 130, 40), 255);
	gfx_circle(x + 30 * f, y + 29 * f, 6 * f, RGB(245, 130, 40), 255);
	float e1[6] = { x + 24.5f * f, y + 28 * f, x + 28 * f, y + 25 * f, x + 24 * f, y + 19 * f };
	float e2[6] = { x + 35.5f * f, y + 28 * f, x + 32 * f, y + 25 * f, x + 36 * f, y + 19 * f };
	gfx_poly(e1, 3, RGB(245, 130, 40), 255);
	gfx_poly(e2, 3, RGB(245, 130, 40), 255);
	gfx_circle(x + 34 * f, y + 44 * f, 4 * f, WHITE, 255);
}

struct app app_runner = {
	.name = "Volpe",
	.icon_top = RGB(120, 200, 255), .icon_bottom = RGB(70, 150, 230),
	.icon = icon, .open = open_, .draw = draw, .event = event, .timer = timer,
};
