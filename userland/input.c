/* OSWiko - input da /dev/input/event* */
#include "input.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_DEV 16
#define QUEUE   64

struct dev {
	int fd;
	char name[64];
	int has_abs;
	struct input_absinfo ax, ay;   /* range assi X/Y */
	int raw_x, raw_y;
	int down, was_down, moved;
};

static struct dev devs[MAX_DEV];
static int ndev;
static int extra_fd[4], n_extra;

void input_watch_fd(int fd)
{
	if (n_extra < 4)
		extra_fd[n_extra++] = fd;
}
static int scr_w, scr_h;

static struct event queue[QUEUE];
static int q_head, q_tail;

static void push(enum ev_type t, int x, int y, int code)
{
	int next = (q_tail + 1) % QUEUE;
	if (next == q_head)
		return; /* coda piena: scarta */
	queue[q_tail] = (struct event){ t, x, y, code };
	q_tail = next;
}

static int pop(struct event *ev)
{
	if (q_head == q_tail)
		return 0;
	*ev = queue[q_head];
	q_head = (q_head + 1) % QUEUE;
	return 1;
}

static int scale(int v, const struct input_absinfo *a, int size)
{
	int range = a->maximum - a->minimum;
	if (range <= 0)
		return v;
	long s = (long)(v - a->minimum) * (size - 1) / range;
	if (s < 0) s = 0;
	if (s >= size) s = size - 1;
	return (int)s;
}

static int test_bit(const unsigned long *bits, int n)
{
	return (bits[n / (8 * sizeof(long))] >> (n % (8 * sizeof(long)))) & 1;
}

int input_init(int screen_w, int screen_h)
{
	DIR *d = opendir("/dev/input");
	struct dirent *de;

	scr_w = screen_w;
	scr_h = screen_h;
	if (!d) {
		perror("/dev/input");
		return 0;
	}
	while ((de = readdir(d)) && ndev < MAX_DEV) {
		char path[300];
		unsigned long absbits[(ABS_MAX + 1) / (8 * sizeof(long)) + 1] = { 0 };

		if (strncmp(de->d_name, "event", 5) != 0)
			continue;
		snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;

		struct dev *v = &devs[ndev];
		memset(v, 0, sizeof(*v));
		v->fd = fd;
		if (ioctl(fd, EVIOCGNAME(sizeof(v->name)), v->name) < 0)
			strcpy(v->name, "?");

		ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
		/* Preferisci gli assi multitouch se presenti (touchscreen reali). */
		if (test_bit(absbits, ABS_MT_POSITION_X) &&
		    test_bit(absbits, ABS_MT_POSITION_Y)) {
			ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &v->ax);
			ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &v->ay);
			v->has_abs = 1;
		} else if (test_bit(absbits, ABS_X) && test_bit(absbits, ABS_Y)) {
			ioctl(fd, EVIOCGABS(ABS_X), &v->ax);
			ioctl(fd, EVIOCGABS(ABS_Y), &v->ay);
			v->has_abs = 1;
		}
		fprintf(stderr, "input: %s \"%s\"%s\n", path, v->name,
			v->has_abs ? " [touch]" : "");
		ndev++;
	}
	closedir(d);
	return ndev;
}

int input_count(void) { return ndev; }
const char *input_name(int i) { return devs[i].name; }

static void handle(struct dev *v, const struct input_event *e)
{
	switch (e->type) {
	case EV_ABS:
		if (e->code == ABS_X || e->code == ABS_MT_POSITION_X) {
			v->raw_x = e->value;
			v->moved = 1;
		} else if (e->code == ABS_Y || e->code == ABS_MT_POSITION_Y) {
			v->raw_y = e->value;
			v->moved = 1;
		} else if (e->code == ABS_MT_TRACKING_ID && e->value == -1) {
			v->down = 0; /* dito sollevato (protocollo MT tipo B) */
		}
		break;
	case EV_KEY:
		if (e->code == BTN_TOUCH || e->code == BTN_LEFT) {
			v->down = e->value != 0;
		} else if (e->code < BTN_MISC) {
			push(e->value == 1 ? IN_KEY : e->value == 2 ? IN_KEY_REPEAT : IN_KEY_UP,
			     0, 0, e->code);
		}
		break;
	case EV_SYN:
		if (e->code != SYN_REPORT || !v->has_abs)
			break;
		int x = scale(v->raw_x, &v->ax, scr_w);
		int y = scale(v->raw_y, &v->ay, scr_h);
		if (v->down && !v->was_down)
			push(IN_TOUCH_DOWN, x, y, 0);
		else if (!v->down && v->was_down)
			push(IN_TOUCH_UP, x, y, 0);
		else if (v->down && v->moved)
			push(IN_TOUCH_MOVE, x, y, 0);
		v->was_down = v->down;
		v->moved = 0;
		break;
	}
}

int input_wait(struct event *ev, int timeout_ms)
{
	struct pollfd pfd[MAX_DEV + 4];

	if (pop(ev))
		return 1;
	for (int i = 0; i < ndev; i++)
		pfd[i] = (struct pollfd){ .fd = devs[i].fd, .events = POLLIN };
	for (int i = 0; i < n_extra; i++)
		pfd[ndev + i] = (struct pollfd){ .fd = extra_fd[i], .events = POLLIN };

	if (poll(pfd, ndev + n_extra, timeout_ms) <= 0)
		return 0;
	for (int i = 0; i < n_extra; i++)
		if (pfd[ndev + i].revents & POLLIN)
			push(IN_FD, 0, 0, extra_fd[i]);

	for (int i = 0; i < ndev; i++) {
		struct input_event buf[32];
		ssize_t n;

		if (!(pfd[i].revents & POLLIN))
			continue;
		while ((n = read(devs[i].fd, buf, sizeof(buf))) > 0)
			for (size_t k = 0; k < (size_t)n / sizeof(buf[0]); k++)
				handle(&devs[i], &buf[k]);
	}
	return pop(ev);
}
