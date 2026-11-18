/* OSWiko - grafica su framebuffer */
#include "gfx.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

struct gfx G;

static int fb_fd = -1;
static uint8_t *fb_mem;
static size_t fb_size;
static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;

int gfx_init(const char *fbdev)
{
	fb_fd = open(fbdev, O_RDWR | O_CLOEXEC);
	if (fb_fd < 0) {
		perror(fbdev);
		return -1;
	}
	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) < 0 ||
	    ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) < 0) {
		perror("FBIOGET_*SCREENINFO");
		return -1;
	}
	/* Accende esplicitamente l'uscita video. Senza console di testo nessuno
	 * lo fa al posto nostro: su alcuni sistemi (QEMU su Windows, telefoni)
	 * lo schermo resterebbe spento anche se disegniamo. */
	var.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
	if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &var) < 0)
		perror("FBIOPUT_VSCREENINFO");
	ioctl(fb_fd, FBIOGET_VSCREENINFO, &var);
	ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix);
	ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
	if (var.bits_per_pixel != 32 && var.bits_per_pixel != 16) {
		fprintf(stderr, "gfx: %u bpp non supportati\n", var.bits_per_pixel);
		return -1;
	}
	fb_size = (size_t)fix.line_length * var.yres_virtual;
	fb_mem = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (fb_mem == MAP_FAILED) {
		perror("mmap fb");
		return -1;
	}
	G.w = var.xres;
	G.h = var.yres;
	G.u = G.w / 360.0f;
	G.back = calloc((size_t)G.w * G.h, sizeof(uint32_t));
	if (!G.back)
		return -1;
	fprintf(stderr, "gfx: %s %dx%d %ubpp stride %u\n", fix.id, G.w, G.h,
		var.bits_per_pixel, fix.line_length);
	return 0;
}

static inline uint32_t chan(uint32_t c, const struct fb_bitfield *f, int shift)
{
	uint32_t v = (c >> shift) & 0xff;
	return (v >> (8 - f->length)) << f->offset;
}

void gfx_present(void)
{
	int fast = var.bits_per_pixel == 32 && var.red.offset == 16 &&
		   var.green.offset == 8 && var.blue.offset == 0;

	for (int y = 0; y < G.h; y++) {
		uint32_t *src = G.back + (size_t)y * G.w;
		uint8_t *dst = fb_mem + (size_t)(y + var.yoffset) * fix.line_length;

		if (fast) {
			memcpy(dst, src, (size_t)G.w * 4);
		} else if (var.bits_per_pixel == 32) {
			uint32_t *d = (uint32_t *)dst;
			for (int x = 0; x < G.w; x++)
				d[x] = chan(src[x], &var.red, 16) |
				       chan(src[x], &var.green, 8) |
				       chan(src[x], &var.blue, 0);
		} else {
			uint16_t *d = (uint16_t *)dst;
			for (int x = 0; x < G.w; x++)
				d[x] = chan(src[x], &var.red, 16) |
				       chan(src[x], &var.green, 8) |
				       chan(src[x], &var.blue, 0);
		}
	}
	ioctl(fb_fd, FBIOPAN_DISPLAY, &var);
}

void gfx_close(void)
{
	if (fb_mem && fb_mem != MAP_FAILED)
		munmap(fb_mem, fb_size);
	if (fb_fd >= 0)
		close(fb_fd);
	free(G.back);
}

/* ---- pixel e fusione ---- */

static inline void blend(uint32_t *p, color_t c, int a)
{
	if (a >= 255) {
		*p = c;
		return;
	}
	if (a <= 0)
		return;
	uint32_t d = *p, ia = 255 - a;
	uint32_t rb = (((c & 0xff00ff) * a + (d & 0xff00ff) * ia) >> 8) & 0xff00ff;
	uint32_t g = (((c & 0x00ff00) * a + (d & 0x00ff00) * ia) >> 8) & 0x00ff00;
	*p = rb | g;
}

static inline void plot(int x, int y, color_t c, int a)
{
	if ((unsigned)x < (unsigned)G.w && (unsigned)y < (unsigned)G.h)
		blend(&G.back[(size_t)y * G.w + x], c, a);
}

color_t gfx_mix(color_t a, color_t b, int t, int max)
{
	if (max <= 0)
		return a;
	int r = (((a >> 16) & 0xff) * (max - t) + ((b >> 16) & 0xff) * t) / max;
	int g = (((a >> 8) & 0xff) * (max - t) + ((b >> 8) & 0xff) * t) / max;
	int bl = ((a & 0xff) * (max - t) + (b & 0xff) * t) / max;
	return RGB(r, g, bl);
}

static inline float clampf(float v)
{
	return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* ---- forme ---- */

void gfx_rect(int x, int y, int w, int h, color_t c, int alpha)
{
	int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
	int x1 = x + w > G.w ? G.w : x + w;
	int y1 = y + h > G.h ? G.h : y + h;

	for (int yy = y0; yy < y1; yy++) {
		uint32_t *row = G.back + (size_t)yy * G.w;
		if (alpha >= 255)
			for (int xx = x0; xx < x1; xx++)
				row[xx] = c;
		else
			for (int xx = x0; xx < x1; xx++)
				blend(&row[xx], c, alpha);
	}
}

void gfx_clear(color_t c)
{
	gfx_rect(0, 0, G.w, G.h, c, 255);
}

/* Copertura (0..1) di un pixel rispetto a un rettangolo arrotondato. */
static float rr_cov(int px, int py, int x, int y, int w, int h, float r)
{
	float fx = px + 0.5f, fy = py + 0.5f;
	float cx, cy;

	if (fx < x + r)
		cx = x + r;
	else if (fx > x + w - r)
		cx = x + w - r;
	else
		return 1;
	if (fy < y + r)
		cy = y + r;
	else if (fy > y + h - r)
		cy = y + h - r;
	else
		return 1;
	float d = sqrtf((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy));
	return clampf(r - d + 0.5f);
}

void gfx_rrect_grad(int x, int y, int w, int h, int r, color_t top,
		    color_t bottom, int alpha)
{
	if (r * 2 > w) r = w / 2;
	if (r * 2 > h) r = h / 2;
	int y0 = y < 0 ? 0 : y, y1 = y + h > G.h ? G.h : y + h;
	int x0 = x < 0 ? 0 : x, x1 = x + w > G.w ? G.w : x + w;

	for (int yy = y0; yy < y1; yy++) {
		color_t c = top == bottom ? top : gfx_mix(top, bottom, yy - y, h - 1);
		uint32_t *row = G.back + (size_t)yy * G.w;
		int corner_row = yy < y + r || yy >= y + h - r;

		for (int xx = x0; xx < x1; xx++) {
			int a = alpha;
			if (corner_row && (xx < x + r || xx >= x + w - r))
				a = (int)(alpha * rr_cov(xx, yy, x, y, w, h, r));
			blend(&row[xx], c, a);
		}
	}
}

void gfx_rrect(int x, int y, int w, int h, int r, color_t c, int alpha)
{
	gfx_rrect_grad(x, y, w, h, r, c, c, alpha);
}

void gfx_shadow(int x, int y, int w, int h, int r, int spread, int alpha)
{
	/* al massimo 4 strati: stesso effetto, molti meno pixel da fondere */
	int step = spread > 4 ? spread / 4 : 1, layers = spread / step;
	for (int i = spread; i > 0; i -= step)
		gfx_rrect(x - i, y - i + spread / 2, w + 2 * i, h + 2 * i, r + i,
			  BLACK, alpha / layers);
}

/* Cerchio pieno: per ogni riga si riempie il tratto interno in blocco e si
 * calcola l'anti-aliasing solo sui pochi pixel del bordo. */
void gfx_circle(float cx, float cy, float r, color_t c, int alpha)
{
	int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
	if (y0 < 0) y0 = 0;
	if (y1 > G.h - 1) y1 = G.h - 1;

	for (int y = y0; y <= y1; y++) {
		float dy = y + 0.5f - cy;
		float ro = r + 1, ri = r - 1;
		if (dy * dy >= ro * ro)
			continue;
		float out = sqrtf(ro * ro - dy * dy);
		float in = ri > 0 && dy * dy < ri * ri ? sqrtf(ri * ri - dy * dy) : 0;
		int xa = (int)floorf(cx - out), xb = (int)ceilf(cx + out);
		int ia = (int)ceilf(cx - in), ib = (int)floorf(cx + in);
		if (in <= 0)
			ia = ib = (int)cx; /* nessun tratto interno */
		uint32_t *row = G.back + (size_t)y * G.w;
		if (in <= 0 || ia >= ib)
			ia = ib = xb + 1; /* tutta la riga è bordo */
		/* bordo sinistro, interno pieno, bordo destro */
		for (int x = xa; x <= xb; x++) {
			if (x == ia) {
				int s0 = ia < 0 ? 0 : ia, e = ib < G.w ? ib : G.w;
				for (int k = s0; k < e; k++)
					blend(&row[k], c, alpha);
				x = ib - 1;
				continue;
			}
			if ((unsigned)x >= (unsigned)G.w)
				continue;
			float dx = x + 0.5f - cx;
			float cov = clampf(r - sqrtf(dx * dx + dy * dy) + 0.5f);
			if (cov > 0)
				blend(&row[x], c, (int)(alpha * cov));
		}
	}
}

void gfx_ring(float cx, float cy, float r, float thick, color_t c, int alpha)
{
	int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
	int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
	float ri = r - thick;

	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float d = sqrtf(dx * dx + dy * dy);
			float cov = fminf(clampf(r - d + 0.5f), clampf(d - ri + 0.5f));
			if (cov > 0)
				plot(x, y, c, (int)(alpha * cov));
		}
}

/* Segmento spesso con estremi arrotondati (capsula). */
void gfx_line(float x0, float y0, float x1, float y1, float thick, color_t c,
	      int alpha)
{
	float hw = thick / 2;
	int bx0 = (int)floorf(fminf(x0, x1) - hw - 1);
	int bx1 = (int)ceilf(fmaxf(x0, x1) + hw + 1);
	int by0 = (int)floorf(fminf(y0, y1) - hw - 1);
	int by1 = (int)ceilf(fmaxf(y0, y1) + hw + 1);
	float vx = x1 - x0, vy = y1 - y0;
	float len2 = vx * vx + vy * vy;

	for (int y = by0; y <= by1; y++)
		for (int x = bx0; x <= bx1; x++) {
			float px = x + 0.5f - x0, py = y + 0.5f - y0;
			float t = len2 > 0 ? (px * vx + py * vy) / len2 : 0;
			t = clampf(t);
			float dx = px - t * vx, dy = py - t * vy;
			float cov = clampf(hw - sqrtf(dx * dx + dy * dy) + 0.5f);
			if (cov > 0)
				plot(x, y, c, (int)(alpha * cov));
		}
}

void gfx_vgradient(int x, int y, int w, int h, color_t top, color_t bottom)
{
	for (int i = 0; i < h; i++)
		gfx_rect(x, y + i, w, 1, gfx_mix(top, bottom, i, h - 1), 255);
}

/* ---- immagini ---- */

int img_alloc(struct image *im, int w, int h)
{
	im->w = w;
	im->h = h;
	im->px = malloc((size_t)w * h * 4);
	return im->px ? 0 : -1;
}

void img_capture(struct image *im)
{
	memcpy(im->px, G.back, (size_t)G.w * G.h * 4);
}

/* Box blur separabile con somma scorrevole: O(pixel), indipendente dal raggio. */
/* Legge n pixel da src con passo "stride" e li scrive contigui in dst. */
static void box_pass(const uint32_t *src, uint32_t *dst, int n, int stride, int r)
{
	int sr = 0, sg = 0, sb = 0, cnt = 2 * r + 1;

	for (int i = -r; i <= r; i++) {
		uint32_t p = src[(i < 0 ? 0 : i >= n ? n - 1 : i) * stride];
		sr += (p >> 16) & 0xff; sg += (p >> 8) & 0xff; sb += p & 0xff;
	}
	for (int i = 0; i < n; i++) {
		dst[i] = RGB(sr / cnt, sg / cnt, sb / cnt);
		int out = i - r, in = i + r + 1;
		uint32_t po = src[(out < 0 ? 0 : out) * stride];
		uint32_t pi = src[(in >= n ? n - 1 : in) * stride];
		sr += ((pi >> 16) & 0xff) - ((po >> 16) & 0xff);
		sg += ((pi >> 8) & 0xff) - ((po >> 8) & 0xff);
		sb += (pi & 0xff) - (po & 0xff);
	}
}

void img_blur(struct image *im, int radius)
{
	int n = im->w > im->h ? im->w : im->h;
	uint32_t *line = malloc((size_t)n * 4);
	if (!line)
		return;
	for (int pass = 0; pass < 3; pass++) {
		for (int y = 0; y < im->h; y++) {
			uint32_t *row = im->px + (size_t)y * im->w;
			box_pass(row, line, im->w, 1, radius);
			memcpy(row, line, (size_t)im->w * 4);
		}
		for (int x = 0; x < im->w; x++) {
			box_pass(im->px + x, line, im->h, im->w, radius);
			for (int y = 0; y < im->h; y++)
				im->px[(size_t)y * im->w + x] = line[y];
		}
	}
	free(line);
}

void gfx_blit(const struct image *im)
{
	memcpy(G.back, im->px, (size_t)G.w * G.h * 4);
}

void gfx_blit_rrect(const struct image *im, int x, int y, int w, int h, int r)
{
	int y0 = y < 0 ? 0 : y, y1 = y + h > G.h ? G.h : y + h;
	int x0 = x < 0 ? 0 : x, x1 = x + w > G.w ? G.w : x + w;

	for (int yy = y0; yy < y1; yy++)
		for (int xx = x0; xx < x1; xx++) {
			float cov = rr_cov(xx, yy, x, y, w, h, r);
			blend(&G.back[(size_t)yy * G.w + xx],
			      im->px[(size_t)yy * im->w + xx], (int)(255 * cov));
		}
}

/* Poligono pieno anti-aliased: 4 sotto-righe per pixel, copertura orizzontale
 * esatta agli estremi di ogni tratto. pts = x0,y0,x1,y1,... */
void gfx_poly(const float *pts, int n, color_t c, int alpha)
{
	static float *acc;
	static int acc_w;
	float xs[64];

	if (n > 64 || n < 3)
		return;
	float minx = pts[0], maxx = pts[0], miny = pts[1], maxy = pts[1];
	for (int i = 1; i < n; i++) {
		minx = fminf(minx, pts[2 * i]); maxx = fmaxf(maxx, pts[2 * i]);
		miny = fminf(miny, pts[2 * i + 1]); maxy = fmaxf(maxy, pts[2 * i + 1]);
	}
	/* ritaglio allo schermo: si lavora solo sulla parte visibile */
	int bx0 = (int)floorf(fmaxf(minx, 0)), bx1 = (int)ceilf(fminf(maxx, G.w - 1));
	int by0 = (int)floorf(fmaxf(miny, 0)), by1 = (int)ceilf(fminf(maxy, G.h - 1));
	if (bx0 > bx1 || by0 > by1)
		return;
	int bw = bx1 - bx0 + 1;
	if (acc_w < G.w + 2) {
		free(acc);
		acc_w = G.w + 2;
		acc = malloc(acc_w * sizeof(float));
		if (!acc) {
			acc_w = 0;
			return;
		}
	}
	for (int y = by0; y <= by1; y++) {
		int lo = bw, hi = -1;
		memset(acc, 0, bw * sizeof(float));
		for (int sub = 0; sub < 4; sub++) {
			float sy = y + (sub + 0.5f) / 4;
			int k = 0;
			for (int i = 0; i < n; i++) {
				float ax = pts[2 * i], ay = pts[2 * i + 1];
				float cx = pts[2 * ((i + 1) % n)], cy = pts[2 * ((i + 1) % n) + 1];
				if ((ay <= sy && cy > sy) || (cy <= sy && ay > sy))
					xs[k++] = ax + (sy - ay) * (cx - ax) / (cy - ay);
			}
			for (int i = 1; i < k; i++)
				for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) {
					float t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t;
				}
			for (int i = 0; i + 1 < k; i += 2) {
				float xa = fmaxf(xs[i] - bx0, 0), xb = fminf(xs[i + 1] - bx0, bw);
				if (xb <= xa)
					continue;
				int ia = (int)xa, ib = (int)xb;
				if (ia < lo) lo = ia;
				if (ib > hi) hi = ib < bw ? ib : bw - 1;
				if (ia == ib) {
					acc[ia] += (xb - xa) / 4;
					continue;
				}
				acc[ia] += (ia + 1 - xa) / 4;
				for (int x = ia + 1; x < ib; x++)
					acc[x] += 0.25f;
				if (ib < bw)
					acc[ib] += (xb - ib) / 4;
			}
		}
		if (hi < 0)
			continue;
		uint32_t *row = G.back + (size_t)y * G.w + bx0;
		for (int x = lo; x <= hi; x++) {
			float a = acc[x];
			if (a >= 0.999f)
				blend(&row[x], c, alpha);
			else if (a > 0)
				blend(&row[x], c, (int)(alpha * a));
		}
	}
}

/* Campionamento bilineare (coordinate in pixel della sorgente). */
uint32_t img_sample(const struct image *im, float x, float y)
{
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x > im->w - 1) x = im->w - 1;
	if (y > im->h - 1) y = im->h - 1;
	int x0 = (int)x, y0 = (int)y;
	int x1 = x0 + 1 < im->w ? x0 + 1 : x0, y1 = y0 + 1 < im->h ? y0 + 1 : y0;
	int fx = (int)((x - x0) * 256), fy = (int)((y - y0) * 256);
	uint32_t a = im->px[(size_t)y0 * im->w + x0], b = im->px[(size_t)y0 * im->w + x1];
	uint32_t c = im->px[(size_t)y1 * im->w + x0], d = im->px[(size_t)y1 * im->w + x1];
	uint32_t out = 0;
	for (int sh = 0; sh <= 16; sh += 8) {
		int top = (((a >> sh) & 0xff) * (256 - fx) + ((b >> sh) & 0xff) * fx) >> 8;
		int bot = (((c >> sh) & 0xff) * (256 - fx) + ((d >> sh) & 0xff) * fx) >> 8;
		out |= (uint32_t)((top * (256 - fy) + bot * fy) >> 8) << sh;
	}
	return out;
}

void gfx_image(const struct image *im, int x, int y, int w, int h, int cover, int r)
{
	if (!im->px || im->w <= 0 || im->h <= 0 || w <= 0 || h <= 0)
		return;
	float sx = (float)w / im->w, sy = (float)h / im->h;
	float s = cover ? fmaxf(sx, sy) : fminf(sx, sy);
	if (!cover) { /* centra nel rettangolo */
		int nw = (int)(im->w * s), nh = (int)(im->h * s);
		x += (w - nw) / 2;
		y += (h - nh) / 2;
		w = nw;
		h = nh;
	}
	float ox = (im->w - w / s) / 2, oy = (im->h - h / s) / 2;
	int y0 = y < 0 ? 0 : y, y1 = y + h > G.h ? G.h : y + h;
	int x0 = x < 0 ? 0 : x, x1 = x + w > G.w ? G.w : x + w;
	/* immagine alla stessa scala: copia diretta, molto più veloce */
	int same = fabsf(s - 1) < 0.001f && r == 0;
	for (int yy = y0; yy < y1; yy++) {
		uint32_t *row = G.back + (size_t)yy * G.w;
		float fy = oy + (yy - y + 0.5f) / s - 0.5f;
		for (int xx = x0; xx < x1; xx++) {
			uint32_t c = same ? im->px[(size_t)(int)(oy + yy - y) * im->w + (int)(ox + xx - x)]
					  : img_sample(im, ox + (xx - x + 0.5f) / s - 0.5f, fy);
			if (r > 0) {
				float cov = rr_cov(xx, yy, x, y, w, h, r);
				if (cov < 1) {
					blend(&row[xx], c, (int)(255 * cov));
					continue;
				}
			}
			row[xx] = c;
		}
	}
}

/* Arco di cerchio spesso: angoli in radianti, 0 = destra, crescono in senso
 * orario (y verso il basso), da a0 ad a1. */
void gfx_arc(float cx, float cy, float r, float thick, float a0, float a1,
	     color_t c, int alpha)
{
	int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
	int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
	float ri = r - thick;
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float d = sqrtf(dx * dx + dy * dy);
			float cov = fminf(clampf(r - d + 0.5f), clampf(d - ri + 0.5f));
			if (cov <= 0)
				continue;
			float a = atan2f(dy, dx);
			if (a < a0) a += 2 * (float)M_PI;
			if (a > a1)
				continue;
			plot(x, y, c, (int)(alpha * cov));
		}
}
