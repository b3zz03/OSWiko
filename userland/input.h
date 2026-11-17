/* OSWiko - input da /dev/input/event* (touchscreen, tasti, tastiera)
 *
 * Unifica touchscreen reali (ABS_MT_*, BTN_TOUCH), tavolette/mouse assoluti
 * di QEMU (ABS_X/ABS_Y, BTN_LEFT) e tasti fisici (Power, Volume) in eventi
 * semplici con coordinate già scalate sullo schermo.
 */
#ifndef OSWIKO_INPUT_H
#define OSWIKO_INPUT_H

enum ev_type {
	IN_NONE,
	IN_TOUCH_DOWN,
	IN_TOUCH_MOVE,
	IN_TOUCH_UP,
	IN_KEY,  /* code = KEY_* di <linux/input-event-codes.h> */
};

struct event {
	enum ev_type type;
	int x, y;
	int code;
};

int  input_init(int screen_w, int screen_h);
/* Attende fino a timeout_ms (-1 = per sempre). Ritorna 1 se c'è un evento. */
int  input_wait(struct event *ev, int timeout_ms);
int  input_count(void);
const char *input_name(int i);

#endif
