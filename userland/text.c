/* OSWiko - testo vettoriale con stb_truetype e cache dei glifi
 *
 * Ogni glifo viene rasterizzato una sola volta per dimensione e poi
 * riusato dalla cache: disegnare testo costa solo la fusione dei pixel.
 */
#include "gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "third_party/stb_truetype.h"
#pragma GCC diagnostic pop

static const char *font_files[FONT_COUNT] = {
	[FONT_LIGHT] = "Inter-Light.otf",
	[FONT_REGULAR] = "Inter-Regular.otf",
	[FONT_SEMIBOLD] = "Inter-SemiBold.otf",
};

struct font {
	unsigned char *data;
	stbtt_fontinfo info;
	int ok;
	int ascent; /* in unità del font */
	int units_per_em;
};

static struct font fonts[FONT_COUNT];

struct glyph {
	uint32_t key_cp;
	uint16_t key_px;
	uint8_t key_font, used;
	int16_t w, h, xoff, yoff;
	float advance;
	unsigned char *bmp;
};

#define CACHE_SIZE 2048 /* potenza di 2 */
static struct glyph cache[CACHE_SIZE];

static unsigned char *read_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *buf = malloc(n);
	if (buf && fread(buf, 1, n, f) != (size_t)n) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	return buf;
}

int text_init(const char *dir)
{
	int loaded = 0;

	for (int i = 0; i < FONT_COUNT; i++) {
		char path[256];
		snprintf(path, sizeof(path), "%s/%s", dir, font_files[i]);
		fonts[i].data = read_file(path);
		if (!fonts[i].data ||
		    !stbtt_InitFont(&fonts[i].info, fonts[i].data,
				    stbtt_GetFontOffsetForIndex(fonts[i].data, 0))) {
			fprintf(stderr, "text: impossibile caricare %s\n", path);
			continue;
		}
		int asc, desc, gap;
		stbtt_GetFontVMetrics(&fonts[i].info, &asc, &desc, &gap);
		fonts[i].ascent = asc;
		fonts[i].ok = 1;
		loaded++;
	}
	/* Se manca un peso, usa il Regular. */
	for (int i = 0; i < FONT_COUNT; i++)
		if (!fonts[i].ok && fonts[FONT_REGULAR].ok)
			fonts[i] = fonts[FONT_REGULAR];
	return loaded;
}

static uint32_t utf8_next(const char **ps)
{
	const unsigned char *s = (const unsigned char *)*ps;
	uint32_t cp;
	int n;

	if (s[0] < 0x80) { cp = s[0]; n = 1; }
	else if ((s[0] & 0xe0) == 0xc0 && s[1]) { cp = s[0] & 0x1f; n = 2; }
	else if ((s[0] & 0xf0) == 0xe0 && s[1] && s[2]) { cp = s[0] & 0x0f; n = 3; }
	else if ((s[0] & 0xf8) == 0xf0 && s[1] && s[2] && s[3]) { cp = s[0] & 0x07; n = 4; }
	else { *ps += 1; return '?'; }
	for (int i = 1; i < n; i++)
		cp = (cp << 6) | (s[i] & 0x3f);
	*ps += n;
	return cp;
}

static struct glyph *get_glyph(enum font_weight w, int px, uint32_t cp)
{
	uint32_t h = (cp * 2654435761u) ^ (px * 40503u) ^ (w * 97u);
	struct font *f = &fonts[w];

	for (int probe = 0; probe < 16; probe++) {
		struct glyph *g = &cache[(h + probe) & (CACHE_SIZE - 1)];
		if (g->used && g->key_cp == cp && g->key_px == px && g->key_font == w)
			return g;
		if (!g->used || probe == 15) {
			/* rasterizza (rimpiazzando l'ultimo slot se la cache è piena) */
			free(g->bmp);
			memset(g, 0, sizeof(*g));
			float scale = stbtt_ScaleForPixelHeight(&f->info, px);
			int adv, lsb, gw = 0, gh = 0, xo = 0, yo = 0;
			stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
			g->bmp = stbtt_GetCodepointBitmap(&f->info, 0, scale, cp,
							  &gw, &gh, &xo, &yo);
			g->w = gw; g->h = gh; g->xoff = xo; g->yoff = yo;
			g->advance = adv * scale;
			g->key_cp = cp; g->key_px = px; g->key_font = w;
			g->used = 1;
			return g;
		}
	}
	return NULL;
}

int text_draw(int x, int y, const char *s, int px, enum font_weight w,
	      color_t c, int alpha)
{
	struct font *f = &fonts[w];
	if (!f->ok || px <= 0)
		return x;
	float scale = stbtt_ScaleForPixelHeight(&f->info, px);
	int baseline = y + (int)(f->ascent * scale + 0.5f);
	float pen = x;

	while (*s) {
		uint32_t cp = utf8_next(&s);
		struct glyph *g = get_glyph(w, px, cp);
		if (!g)
			continue;
		int gx = (int)(pen + 0.5f) + g->xoff, gy = baseline + g->yoff;
		for (int j = 0; j < g->h; j++) {
			int yy = gy + j;
			if ((unsigned)yy >= (unsigned)G.h)
				continue;
			for (int i = 0; i < g->w; i++) {
				int cov = g->bmp[j * g->w + i];
				int xx = gx + i;
				if (cov && (unsigned)xx < (unsigned)G.w) {
					uint32_t *p = &G.back[(size_t)yy * G.w + xx];
					int a = cov * alpha / 255;
					uint32_t d = *p, ia = 255 - a;
					uint32_t rb = (((c & 0xff00ff) * a + (d & 0xff00ff) * ia) >> 8) & 0xff00ff;
					uint32_t gg = (((c & 0xff00) * a + (d & 0xff00) * ia) >> 8) & 0xff00;
					*p = rb | gg;
				}
			}
		}
		pen += g->advance;
	}
	return (int)(pen + 0.5f);
}

int text_width(const char *s, int px, enum font_weight w)
{
	float pen = 0;

	if (!fonts[w].ok)
		return 0;
	while (*s) {
		struct glyph *g = get_glyph(w, px, utf8_next(&s));
		if (g)
			pen += g->advance;
	}
	return (int)(pen + 0.5f);
}

void text_center(int cx, int y, const char *s, int px, enum font_weight w,
		 color_t c, int alpha)
{
	text_draw(cx - text_width(s, px, w) / 2, y, s, px, w, c, alpha);
}

void text_right(int rx, int y, const char *s, int px, enum font_weight w,
		color_t c, int alpha)
{
	text_draw(rx - text_width(s, px, w), y, s, px, w, c, alpha);
}
