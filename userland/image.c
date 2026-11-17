/* OSWiko - immagini: lettura JPEG/PNG, scrittura JPEG, ridimensionamento */
#include "gfx.h"

#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#pragma GCC diagnostic ignored "-Wunused-function" /* a fine file */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "third_party/stb_image.h"
#include "third_party/stb_image_write.h"
#pragma GCC diagnostic pop

void img_free(struct image *im)
{
	free(im->px);
	im->px = NULL;
	im->w = im->h = 0;
}

int img_info(const char *path, int *w, int *h)
{
	int c;
	return stbi_info(path, w, h, &c) ? 0 : -1;
}

/* Carica un'immagine riducendola (se serve) per stare in max_w x max_h.
 * La riduzione fa la media dei pixel (niente "scalini"). */
int img_load_file(struct image *im, const char *path, int max_w, int max_h)
{
	int w, h, c;
	unsigned char *rgb = stbi_load(path, &w, &h, &c, 3);
	if (!rgb)
		return -1;
	int f = 1; /* fattore di riduzione intero */
	while (w / (f + 1) >= max_w || h / (f + 1) >= max_h)
		f++;
	if (w / f > max_w || h / f > max_h)
		f++;
	int ow = w / f, oh = h / f;
	if (ow < 1) ow = 1;
	if (oh < 1) oh = 1;
	if (img_alloc(im, ow, oh) < 0) {
		stbi_image_free(rgb);
		return -1;
	}
	for (int y = 0; y < oh; y++)
		for (int x = 0; x < ow; x++) {
			unsigned r = 0, g = 0, b = 0;
			for (int dy = 0; dy < f; dy++) {
				const unsigned char *p = rgb + ((size_t)(y * f + dy) * w + x * f) * 3;
				for (int dx = 0; dx < f; dx++, p += 3) {
					r += p[0]; g += p[1]; b += p[2];
				}
			}
			unsigned n = f * f;
			im->px[(size_t)y * ow + x] = RGB(r / n, g / n, b / n);
		}
	stbi_image_free(rgb);
	return 0;
}

int img_save_jpeg(const struct image *im, const char *path, int quality)
{
	unsigned char *rgb = malloc((size_t)im->w * im->h * 3);
	if (!rgb)
		return -1;
	for (size_t i = 0; i < (size_t)im->w * im->h; i++) {
		rgb[i * 3] = im->px[i] >> 16;
		rgb[i * 3 + 1] = im->px[i] >> 8;
		rgb[i * 3 + 2] = im->px[i];
	}
	int ok = stbi_write_jpg(path, im->w, im->h, 3, rgb, quality);
	free(rgb);
	return ok ? 0 : -1;
}

/* Nuova immagine w x h che "copre" la sorgente (ritaglio centrale). */
int img_resize_cover(const struct image *src, struct image *dst, int w, int h)
{
	if (img_alloc(dst, w, h) < 0)
		return -1;
	float s = (float)w / src->w > (float)h / src->h ? (float)w / src->w : (float)h / src->h;
	float ox = (src->w - w / s) / 2, oy = (src->h - h / s) / 2;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			dst->px[(size_t)y * w + x] = img_sample(src, ox + (x + 0.5f) / s - 0.5f,
								oy + (y + 0.5f) / s - 0.5f);
	return 0;
}
