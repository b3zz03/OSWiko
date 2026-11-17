/* OSWiko - grafica su framebuffer (/dev/fb0)
 *
 * Si disegna sempre su un back buffer XRGB8888 in RAM; gfx_present()
 * copia il frame sullo schermo convertendo al formato reale del display.
 * Tutte le forme sono anti-aliased e accettano un'opacità 0..255.
 */
#ifndef OSWIKO_GFX_H
#define OSWIKO_GFX_H

#include <stdint.h>

typedef uint32_t color_t; /* 0xRRGGBB */

#define RGB(r, g, b) ((color_t)(((r) << 16) | ((g) << 8) | (b)))
#define WHITE RGB(255, 255, 255)
#define BLACK RGB(0, 0, 0)

struct image {
	int w, h;
	uint32_t *px;
};

struct gfx {
	int w, h;        /* risoluzione visibile */
	float u;         /* scala: 1 "punto" = u pixel (schermo largo 360 punti) */
	uint32_t *back;  /* back buffer, w*h pixel */
};

extern struct gfx G;

/* Converte punti logici in pixel. */
#define P(v) ((int)((v) * G.u + 0.5f))

int  gfx_init(const char *fbdev);
void gfx_present(void);
void gfx_close(void);

color_t gfx_mix(color_t a, color_t b, int t, int max);

void gfx_clear(color_t c);
void gfx_rect(int x, int y, int w, int h, color_t c, int alpha);
void gfx_rrect(int x, int y, int w, int h, int r, color_t c, int alpha);
void gfx_rrect_grad(int x, int y, int w, int h, int r,
		    color_t top, color_t bottom, int alpha);
void gfx_shadow(int x, int y, int w, int h, int r, int spread, int alpha);
void gfx_circle(float cx, float cy, float r, color_t c, int alpha);
void gfx_ring(float cx, float cy, float r, float thick, color_t c, int alpha);
void gfx_line(float x0, float y0, float x1, float y1, float thick,
	      color_t c, int alpha);
void gfx_vgradient(int x, int y, int w, int h, color_t top, color_t bottom);
void gfx_poly(const float *pts, int n, color_t c, int alpha);

/* Immagini */
int  img_alloc(struct image *im, int w, int h);
void img_capture(struct image *im);          /* copia il back buffer */
void img_blur(struct image *im, int radius); /* sfocatura (3 box blur) */
void gfx_blit(const struct image *im);       /* immagine a pieno schermo */
void gfx_blit_rrect(const struct image *im, int x, int y, int w, int h, int r);
/* Disegna l'immagine scalata nel rettangolo: cover=1 riempie e ritaglia,
 * cover=0 la fa stare tutta (bande ai lati). r = raggio degli angoli. */
void gfx_image(const struct image *im, int x, int y, int w, int h, int cover, int r);
uint32_t img_sample(const struct image *im, float x, float y); /* bilineare */

/* image.c: file */
void img_free(struct image *im);
int  img_info(const char *path, int *w, int *h);
int  img_load_file(struct image *im, const char *path, int max_w, int max_h);
int  img_save_jpeg(const struct image *im, const char *path, int quality);
int  img_resize_cover(const struct image *src, struct image *dst, int w, int h);

/* Testo vettoriale (font Inter, anti-aliasing). y = bordo superiore. */
enum font_weight { FONT_LIGHT, FONT_REGULAR, FONT_SEMIBOLD, FONT_COUNT };

int  text_init(const char *dir);
int  text_draw(int x, int y, const char *s, int px, enum font_weight w,
	       color_t c, int alpha);
int  text_width(const char *s, int px, enum font_weight w);
void text_center(int cx, int y, const char *s, int px, enum font_weight w,
		 color_t c, int alpha);
void text_right(int rx, int y, const char *s, int px, enum font_weight w,
		color_t c, int alpha);

#endif
