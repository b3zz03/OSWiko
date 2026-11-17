/* OSWiko - Balzo: platform a scorrimento automatico con un solo tasto.
 *
 * Il cubo corre da solo: tocca (o tieni premuto) per saltare oltre
 * spuntoni, blocchi e burroni. Il percorso è generato da un seme fisso,
 * quindi ogni tentativo è uguale e si può imparare a memoria.
 */
#include "ui.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define FRAME_MS  33
#define MAX_OBS   256
#define MAX_PART  40
#define TRAIL     6

#define SPEED0    8.0f   /* caselle al secondo */
#define GRAVITY   80.0f
#define JUMP_V    19.5f
#define PAD_V     27.0f

enum { BLOCK, SPIKE, PAD, GAP };

struct obs { int type; float x, y, w, h; };
struct part { float x, y, vx, vy, life; color_t c; };

static struct obs obs[MAX_OBS];
static int n_obs;
static struct part parts[MAX_PART];
static float trail_x[TRAIL], trail_y[TRAIL];

static float px, py, vy, rot, speed;   /* giocatore (in caselle) */
static int on_ground, holding, dead, started;
static long long dead_at, last_ms;
static float gen_x;                    /* fin dove è stato generato il percorso */
static unsigned seed;
static int attempt = 1, best_m;

/* palette dello sfondo: cambiano man mano che si avanza */
static const color_t sky_top[] = { RGB(255, 120, 150), RGB(80, 120, 255), RGB(40, 200, 170), RGB(170, 90, 255) };
static const color_t sky_bot[] = { RGB(255, 200, 120), RGB(140, 220, 255), RGB(200, 255, 160), RGB(255, 150, 200) };
#define NPAL 4

static unsigned rnd(void)
{
	seed = seed * 1103515245u + 12345u;
	return (seed >> 16) & 0x7fff;
}

static void add(int type, float x, float y, float w, float h)
{
	if (n_obs < MAX_OBS)
		obs[n_obs++] = (struct obs){ type, x, y, w, h };
}

/* Aggiunge un "pezzo" di percorso a partire da x. Ritorna la lunghezza. */
static float pattern(float x)
{
	int level = (int)(x / 120); /* difficoltà crescente */
	int n = 7 + (level > 3 ? 3 : level);
	switch (rnd() % n) {
	case 0: add(SPIKE, x, 0, 1, 1); return 1;
	case 1: add(SPIKE, x, 0, 1, 1); add(SPIKE, x + 1, 0, 1, 1); return 2;
	case 2: add(BLOCK, x, 0, 1, 1); return 1;
	case 3: /* scalino e discesa */
		add(BLOCK, x, 0, 2, 1); add(BLOCK, x + 4, 0, 2, 2);
		return 6;
	case 4: /* piattaforma con spuntone sopra */
		add(BLOCK, x, 0, 7, 1); add(SPIKE, x + 3.5f, 1, 1, 1);
		return 7;
	case 5: add(GAP, x, 0, 2, 0); return 2;
	case 6: /* pedana + fila di spuntoni */
		add(PAD, x, 0, 1, 0.3f);
		for (int i = 0; i < 3; i++)
			add(SPIKE, x + 1.5f + i, 0, 1, 1);
		return 6;
	case 7: add(SPIKE, x, 0, 1, 1); add(SPIKE, x + 1, 0, 1, 1); add(SPIKE, x + 2, 0, 1, 1); return 3;
	case 8: add(GAP, x, 0, 3, 0); return 3;
	default: /* torre con spuntoni davanti */
		add(SPIKE, x, 0, 1, 1); add(BLOCK, x + 1, 0, 2, 2); add(SPIKE, x + 6, 0, 1, 1);
		return 7;
	}
}

static void generate(float upto)
{
	/* scarta gli ostacoli già superati */
	int k = 0;
	for (int i = 0; i < n_obs; i++)
		if (obs[i].x + obs[i].w > px - 6)
			obs[k++] = obs[i];
	n_obs = k;
	while (gen_x < upto && n_obs < MAX_OBS - 8) {
		gen_x += pattern(gen_x);
		gen_x += 4 + rnd() % 4;
	}
}

static void reset(void)
{
	seed = 20240917u; /* stesso percorso a ogni tentativo */
	n_obs = 0;
	gen_x = 12;
	px = 0; py = 0; vy = 0; rot = 0;
	speed = SPEED0;
	on_ground = 1;
	dead = 0;
	for (int i = 0; i < TRAIL; i++)
		trail_x[i] = px, trail_y[i] = py;
	for (int i = 0; i < MAX_PART; i++)
		parts[i].life = 0;
	generate(px + 40);
}

static void die(void)
{
	dead = 1;
	dead_at = ui_now_ms();
	for (int i = 0; i < MAX_PART; i++) {
		float a = (rnd() % 628) / 100.0f, v = 3 + (rnd() % 100) / 12.0f;
		parts[i] = (struct part){ px + 0.5f, py + 0.5f, cosf(a) * v, sinf(a) * v + 4,
					  1, i % 2 ? RGB(255, 170, 40) : WHITE };
	}
	int m = (int)px;
	if (m > best_m)
		best_m = m;
}

static int over_gap(float x)
{
	for (int i = 0; i < n_obs; i++)
		if (obs[i].type == GAP && x > obs[i].x && x < obs[i].x + obs[i].w)
			return 1;
	return 0;
}

static void step(float dt)
{
	float prev_y = py;

	px += speed * dt;
	speed = SPEED0 + px / 200; /* accelera piano piano */
	if (speed > 13)
		speed = 13;

	if (on_ground && holding) {
		vy = JUMP_V;
		on_ground = 0;
	}
	vy -= GRAVITY * dt;
	py += vy * dt;

	/* appoggio: terreno (se non c'è un burrone) */
	int was_ground = on_ground;
	on_ground = 0;
	if (!over_gap(px + 0.5f) && py <= 0 && prev_y >= -0.3f) {
		py = 0;
		vy = 0;
		on_ground = 1;
	}

	/* ostacoli: hitbox del cubo leggermente ridotta */
	float l = px + 0.12f, r = px + 0.88f;
	for (int i = 0; i < n_obs && !dead; i++) {
		struct obs *o = &obs[i];
		if (o->x > r || o->x + o->w < l)
			continue;
		switch (o->type) {
		case BLOCK: {
			float top = o->y + o->h;
			if (py < top && py + 1 > o->y) {
				if (prev_y >= top - 0.3f && vy <= 0) {
					py = top; /* atterra sopra */
					vy = 0;
					on_ground = 1;
				} else {
					die();
				}
			}
			break;
		}
		case SPIKE:
			if (px + 0.85f > o->x + 0.3f && px + 0.15f < o->x + 0.7f &&
			    py < o->y + 0.6f && py + 1 > o->y)
				die();
			break;
		case PAD:
			if (py <= o->y + o->h + 0.05f) {
				vy = PAD_V;
				on_ground = 0;
			}
			break;
		}
	}
	if (py < -3)
		die();

	/* rotazione: gira in aria, si raddrizza a terra */
	if (!on_ground) {
		rot += 400 * dt;
	} else {
		float target = roundf(rot / 90) * 90;
		rot += (target - rot) * 0.5f;
		if (!was_ground)
			rot = target;
	}
}

static void open_(void)
{
	reset();
	started = 0;
	attempt = 1;
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

	if (started && !dead) {
		/* due sotto-passi per collisioni più precise */
		step(dt / 2);
		if (!dead)
			step(dt / 2);
		generate(px + 40);
		for (int i = TRAIL - 1; i > 0; i--)
			trail_x[i] = trail_x[i - 1], trail_y[i] = trail_y[i - 1];
		trail_x[0] = px;
		trail_y[0] = py;
	}
	for (int i = 0; i < MAX_PART; i++) {
		struct part *p = &parts[i];
		if (p->life <= 0)
			continue;
		p->vy -= 25 * dt;
		p->x += p->vx * dt;
		p->y += p->vy * dt;
		p->life -= dt * 1.2f;
	}
	if (dead && now - dead_at > 1000) {
		attempt++;
		reset();
	}
	ui_redraw();
	ui_timer(FRAME_MS);
}

/* ---- disegno ---- */

static int T;       /* dimensione di una casella in pixel */
static int GY;      /* y del terreno sullo schermo */
static float camx;

static int sx(float x) { return (int)((x - camx) * T); }
static int sy(float y) { return GY - (int)(y * T); }

static void draw_cube(float x, float y, float angle, int alpha, int face)
{
	float cx = sx(x) + T / 2.0f, cy = sy(y) - T / 2.0f;
	float a = angle * (float)M_PI / 180, h = T * 0.5f;
	float c = cosf(a), s = sinf(a);
	float pts[8];
	static const float corner[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };

	for (int i = 0; i < 4; i++) {
		float ox = corner[i][0] * h, oy = corner[i][1] * h;
		pts[2 * i] = cx + ox * c - oy * s;
		pts[2 * i + 1] = cy + ox * s + oy * c;
	}
	gfx_poly(pts, 4, RGB(40, 30, 60), alpha);
	for (int i = 0; i < 4; i++) {
		float ox = corner[i][0] * h * 0.78f, oy = corner[i][1] * h * 0.78f;
		pts[2 * i] = cx + ox * c - oy * s;
		pts[2 * i + 1] = cy + ox * s + oy * c;
	}
	gfx_poly(pts, 4, RGB(255, 170, 40), alpha);
	if (!face)
		return;
	/* occhi e sorriso, ruotano con il cubo */
	float ex[2] = { -0.3f, 0.3f };
	for (int i = 0; i < 2; i++) {
		float ox = ex[i] * h, oy = -0.2f * h;
		gfx_circle(cx + ox * c - oy * s, cy + ox * s + oy * c, T * 0.09f,
			   RGB(40, 30, 60), alpha);
	}
	float m0x = -0.3f * h, m1x = 0.3f * h, my = 0.3f * h;
	gfx_line(cx + m0x * c - my * s, cy + m0x * s + my * c,
		 cx + m1x * c - my * s, cy + m1x * s + my * c, T * 0.07f, RGB(40, 30, 60), alpha);
}

static void draw(void)
{
	char buf[48];

	T = P(32);
	GY = (int)(G.h * 0.66f);
	camx = px - 2.5f;

	/* cielo: la palette sfuma ogni 150 caselle */
	float seg = px / 150;
	int i0 = (int)seg % NPAL, i1 = (i0 + 1) % NPAL;
	int t = (int)((seg - floorf(seg)) * 100);
	color_t top = gfx_mix(sky_top[i0], sky_top[i1], t, 100);
	color_t bot = gfx_mix(sky_bot[i0], sky_bot[i1], t, 100);
	gfx_vgradient(0, 0, G.w, GY, top, bot);

	/* parallasse: colonne lontane */
	for (int k = -1; k < 12; k++) {
		float base = floorf(camx * 0.3f / 3) * 3;
		float wx = base + k * 3;
		unsigned hsh = (unsigned)(wx * 7919) * 2654435761u;
		int h = P(60 + (hsh >> 24) % 120);
		int x = (int)((wx - camx * 0.3f) * T);
		gfx_rrect(x, GY - h, T * 2, h + P(10), P(8), WHITE, 45);
	}

	/* terreno (con i burroni) */
	color_t ground = gfx_mix(top, BLACK, 70, 100);
	gfx_rect(0, GY, G.w, G.h - GY, ground, 255);
	for (int i = 0; i < n_obs; i++)
		if (obs[i].type == GAP)
			gfx_rect(sx(obs[i].x), GY, (int)(obs[i].w * T), G.h - GY,
				 gfx_mix(ground, BLACK, 60, 100), 255);
	gfx_rect(0, GY, G.w, P(3), WHITE, 200);
	for (int i = 0; i < n_obs; i++)
		if (obs[i].type == GAP)
			gfx_rect(sx(obs[i].x), GY, (int)(obs[i].w * T), P(3),
				 gfx_mix(ground, BLACK, 60, 100), 255);
	for (float gx = floorf(camx / 2) * 2; gx < camx + G.w / (float)T + 2; gx += 2)
		gfx_rect(sx(gx), GY + P(3), P(2), G.h - GY, WHITE, 18);

	/* ostacoli */
	for (int i = 0; i < n_obs; i++) {
		struct obs *o = &obs[i];
		int x = sx(o->x), y = sy(o->y + o->h), w = (int)(o->w * T), h = (int)(o->h * T);
		if (x > G.w || x + w < 0)
			continue;
		switch (o->type) {
		case BLOCK:
			gfx_rrect(x, y, w, h, P(5), RGB(40, 30, 60), 255);
			gfx_rrect(x + P(3), y + P(3), w - P(6), h - P(6), P(3),
				  gfx_mix(top, WHITE, 40, 100), 255);
			gfx_rect(x + P(3), y + P(3), w - P(6), P(3), WHITE, 90);
			break;
		case SPIKE: {
			float pts[6] = { x + T * 0.08f, (float)sy(o->y), x + T / 2.0f, (float)y + T * 0.05f,
					 x + T * 0.92f, (float)sy(o->y) };
			gfx_poly(pts, 3, RGB(40, 30, 60), 255);
			float in[6] = { x + T * 0.28f, sy(o->y) - T * 0.08f, x + T / 2.0f, y + T * 0.38f,
					x + T * 0.72f, sy(o->y) - T * 0.08f };
			gfx_poly(in, 3, WHITE, 200);
			break;
		}
		case PAD:
			gfx_rrect(x + P(2), sy(o->y) - P(8), w - P(4), P(8), P(4), RGB(255, 220, 0), 255);
			gfx_circle(x + w / 2.0f, sy(o->y) - P(14), P(4), RGB(255, 220, 0), 200);
			break;
		}
	}

	/* scia e cubo */
	if (!dead) {
		for (int i = TRAIL - 1; i > 0; i--)
			draw_cube(trail_x[i], trail_y[i], rot, 40 - i * 6, 0);
		draw_cube(px, py, rot, 255, 1);
	}
	for (int i = 0; i < MAX_PART; i++)
		if (parts[i].life > 0)
			gfx_rrect(sx(parts[i].x), sy(parts[i].y), P(7), P(7), P(2), parts[i].c,
				  (int)(255 * parts[i].life));

	/* punteggio */
	snprintf(buf, sizeof(buf), "%d m", (int)px);
	text_center(G.w / 2, P(60), buf, P(44), FONT_SEMIBOLD, WHITE, 240);
	snprintf(buf, sizeof(buf), "Tentativo %d   •   Record %d m", attempt, best_m);
	text_center(G.w / 2, P(112), buf, P(14), FONT_REGULAR, WHITE, 210);

	if (!started) {
		text_center(G.w / 2, GY + P(60), "Balzo", P(48), FONT_SEMIBOLD, WHITE, 255);
		text_center(G.w / 2, GY + P(124), "Tocca per saltare", P(18), FONT_REGULAR, WHITE, 230);
		text_center(G.w / 2, GY + P(150), "tieni premuto per saltare di continuo",
			    P(13), FONT_REGULAR, WHITE, 180);
	}
}

static void event(const struct event *ev)
{
	if (ev->type == IN_TOUCH_DOWN ||
	    (ev->type == IN_KEY && (ev->code == KEY_SPACE || ev->code == KEY_UP))) {
		if (!started) {
			started = 1;
			last_ms = ui_now_ms();
		}
		holding = 1;
		if (ev->type == IN_KEY)
			holding = 2; /* da tastiera: un salto solo */
	} else if (ev->type == IN_TOUCH_UP) {
		holding = 0;
	}
	/* un tasto della tastiera non ha "rilascio": salta una volta */
	if (holding == 2 && on_ground && !dead && started) {
		vy = JUMP_V;
		on_ground = 0;
		holding = 0;
	}
}

static void close_(void) { holding = 0; }

static void icon(int x, int y, int s)
{
	float f = s / 60.0f;
	gfx_rect(x, y + 44 * f, s, 16 * f, RGB(60, 30, 90), 255);
	float sp[6] = { x + 34 * f, y + 44 * f, x + 41 * f, y + 32 * f, x + 48 * f, y + 44 * f };
	gfx_poly(sp, 3, WHITE, 255);
	gfx_rrect(x + 12 * f, y + 18 * f, 17 * f, 17 * f, 3 * f, RGB(40, 30, 60), 255);
	gfx_rrect(x + 14.5f * f, y + 20.5f * f, 12 * f, 12 * f, 2 * f, RGB(255, 170, 40), 255);
	gfx_circle(x + 18 * f, y + 25 * f, 1.4f * f, RGB(40, 30, 60), 255);
	gfx_circle(x + 23 * f, y + 25 * f, 1.4f * f, RGB(40, 30, 60), 255);
}

struct app app_jump = {
	.name = "Balzo",
	.icon_top = RGB(255, 110, 150), .icon_bottom = RGB(170, 80, 255),
	.icon = icon, .open = open_, .draw = draw, .event = event,
	.timer = timer, .close = close_,
};
