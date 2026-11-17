/* OSWiko - acquisizione video con V4L2 (l'interfaccia standard di Linux
 * per le videocamere). Formato YUYV, buffer in memoria condivisa. */
#include "gfx.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define NBUF 3

static int fd = -1;
static void *buf[NBUF];
static size_t buf_len[NBUF];
static int cam_w, cam_h, stride;

void cam_close(void)
{
	if (fd < 0)
		return;
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ioctl(fd, VIDIOC_STREAMOFF, &type);
	for (int i = 0; i < NBUF; i++)
		if (buf[i] && buf[i] != MAP_FAILED)
			munmap(buf[i], buf_len[i]);
	memset(buf, 0, sizeof(buf));
	close(fd);
	fd = -1;
}

int cam_open(int *w, int *h)
{
	struct v4l2_format f;
	struct v4l2_requestbuffers rb;

	fd = open("/dev/video0", O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return -1;
	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f.fmt.pix.width = 640;
	f.fmt.pix.height = 480;
	f.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	f.fmt.pix.field = V4L2_FIELD_ANY;
	if (ioctl(fd, VIDIOC_S_FMT, &f) < 0 || f.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
		fprintf(stderr, "camera: formato YUYV non disponibile\n");
		goto fail;
	}
	cam_w = f.fmt.pix.width;
	cam_h = f.fmt.pix.height;
	stride = f.fmt.pix.bytesperline ? (int)f.fmt.pix.bytesperline : cam_w * 2;

	memset(&rb, 0, sizeof(rb));
	rb.count = NBUF;
	rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	rb.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_REQBUFS, &rb) < 0 || rb.count < 1)
		goto fail;
	for (unsigned i = 0; i < rb.count && i < NBUF; i++) {
		struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
					 .memory = V4L2_MEMORY_MMAP, .index = i };
		if (ioctl(fd, VIDIOC_QUERYBUF, &b) < 0)
			goto fail;
		buf_len[i] = b.length;
		buf[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
		if (buf[i] == MAP_FAILED || ioctl(fd, VIDIOC_QBUF, &b) < 0)
			goto fail;
	}
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd, VIDIOC_STREAMON, &type) < 0)
		goto fail;
	*w = cam_w;
	*h = cam_h;
	fprintf(stderr, "camera: /dev/video0 %dx%d\n", cam_w, cam_h);
	return 0;
fail:
	cam_close();
	return -1;
}

static inline int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* Prende l'ultimo fotogramma disponibile (se c'è) e lo converte in RGB. */
int cam_grab(struct image *dst)
{
	struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
	if (fd < 0 || ioctl(fd, VIDIOC_DQBUF, &b) < 0)
		return 0;
	const unsigned char *src = buf[b.index];
	for (int y = 0; y < cam_h && y < dst->h; y++) {
		const unsigned char *p = src + (size_t)y * stride;
		uint32_t *o = dst->px + (size_t)y * dst->w;
		for (int x = 0; x + 1 < cam_w && x + 1 < dst->w; x += 2, p += 4) {
			int y0 = p[0] - 16, u = p[1] - 128, y1 = p[2] - 16, v = p[3] - 128;
			int r = 409 * v, g = -100 * u - 208 * v, bl = 516 * u;
			int c0 = 298 * y0, c1 = 298 * y1;
			o[x] = RGB(clamp8((c0 + r + 128) >> 8), clamp8((c0 + g + 128) >> 8),
				   clamp8((c0 + bl + 128) >> 8));
			o[x + 1] = RGB(clamp8((c1 + r + 128) >> 8), clamp8((c1 + g + 128) >> 8),
				       clamp8((c1 + bl + 128) >> 8));
		}
	}
	ioctl(fd, VIDIOC_QBUF, &b);
	return 1;
}
