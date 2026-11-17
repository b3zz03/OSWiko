/* OSWiko - archivio foto */
#include "photos.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct entry { char name[64]; time_t mtime; };

static int cmp_entry(const void *a, const void *b)
{
	const struct entry *x = a, *y = b;
	if (x->mtime != y->mtime)
		return x->mtime < y->mtime ? 1 : -1;
	return strcmp(y->name, x->name);
}

void photo_path(const char *name, char *out, int n)
{
	snprintf(out, n, "%s/%s", PHOTO_DIR, name);
}

int photos_list(char (*names)[64], int max)
{
	static struct entry e[PHOTO_MAX * 2];
	int n = 0;
	DIR *d = opendir(PHOTO_DIR);
	struct dirent *de;

	if (!d)
		return 0;
	while ((de = readdir(d)) && n < PHOTO_MAX * 2) {
		size_t l = strlen(de->d_name);
		if (de->d_name[0] == '.' || l < 5 || l >= 64 || strcmp(de->d_name + l - 4, ".jpg"))
			continue;
		char p[128];
		struct stat st;
		photo_path(de->d_name, p, sizeof(p));
		if (stat(p, &st) < 0)
			continue;
		snprintf(e[n].name, sizeof(e[n].name), "%.63s", de->d_name);
		e[n].mtime = st.st_mtime;
		n++;
	}
	closedir(d);
	qsort(e, n, sizeof(e[0]), cmp_entry);
	if (n > max)
		n = max;
	for (int i = 0; i < n; i++)
		memcpy(names[i], e[i].name, 64);
	return n;
}

int photo_save(const struct image *im, char *out, int n)
{
	time_t t = time(NULL);
	struct tm tm;
	char name[64];

	mkdir("/data", 0755);
	mkdir(PHOTO_DIR, 0755);
	localtime_r(&t, &tm);
	for (int k = 0; k < 100; k++) {
		int l = strftime(name, sizeof(name), "IMG_%Y%m%d_%H%M%S", &tm);
		if (k)
			snprintf(name + l, sizeof(name) - l, "_%d", k);
		strcat(name, ".jpg");
		photo_path(name, out, n);
		if (access(out, F_OK) != 0)
			break;
	}
	if (img_save_jpeg(im, out, 88) < 0)
		return -1;
	sync(); /* una foto appena scattata non deve andare persa */
	return 0;
}

int photo_thumb(const char *name, struct image *out)
{
	char tp[160], pp[128];
	snprintf(tp, sizeof(tp), "%s/%s", THUMB_DIR, name);
	if (img_load_file(out, tp, THUMB_SIZE, THUMB_SIZE) == 0)
		return 0;
	/* non c'è: creala dalla foto */
	struct image big = { 0 };
	photo_path(name, pp, sizeof(pp));
	if (img_load_file(&big, pp, THUMB_SIZE * 2, THUMB_SIZE * 2) < 0)
		return -1;
	int r = img_resize_cover(&big, out, THUMB_SIZE, THUMB_SIZE);
	img_free(&big);
	if (r == 0) {
		mkdir(THUMB_DIR, 0755);
		img_save_jpeg(out, tp, 80);
	}
	return r;
}

void photo_delete(const char *name)
{
	char p[160];
	photo_path(name, p, sizeof(p));
	unlink(p);
	snprintf(p, sizeof(p), "%s/%s", THUMB_DIR, name);
	unlink(p);
	sync();
}
