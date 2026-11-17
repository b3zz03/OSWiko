/* OSWiko - tastiera a schermo in stile iOS, layout italiano */
#include "keyboard.h"
#include "ui.h"

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <string.h>

enum { K_CHAR, K_SHIFT, K_BACK, K_PAGE, K_SPACE, K_ENTER };

struct key {
	const char *label;
	const char *out;   /* testo da inserire (K_CHAR) */
	float w;           /* larghezza in "tasti" */
	int kind;
	int page;          /* pagina di destinazione (K_PAGE) */
};

#define C(s) { s, s, 1, K_CHAR, 0 }
#define ROW_END { NULL, NULL, 0, 0, 0 }

enum { PAGE_ABC, PAGE_123, PAGE_ACC, N_PAGES };

static const struct key pages[N_PAGES][4][12] = {
	[PAGE_ABC] = {
		{ C("q"), C("w"), C("e"), C("r"), C("t"), C("y"), C("u"), C("i"), C("o"), C("p"), ROW_END },
		{ C("a"), C("s"), C("d"), C("f"), C("g"), C("h"), C("j"), C("k"), C("l"), ROW_END },
		{ { "⇧", NULL, 1.4f, K_SHIFT, 0 }, C("z"), C("x"), C("c"), C("v"), C("b"), C("n"), C("m"),
		  { "⌫", NULL, 1.4f, K_BACK, 0 }, ROW_END },
		{ { "123", NULL, 1.5f, K_PAGE, PAGE_123 }, { "àè", NULL, 1.2f, K_PAGE, PAGE_ACC },
		  { "spazio", " ", 4.6f, K_SPACE, 0 }, { "Invia", NULL, 2.2f, K_ENTER, 0 }, ROW_END },
	},
	[PAGE_123] = {
		{ C("1"), C("2"), C("3"), C("4"), C("5"), C("6"), C("7"), C("8"), C("9"), C("0"), ROW_END },
		{ C("-"), C("/"), C(":"), C(";"), C("("), C(")"), C("€"), C("&"), C("@"), C("\""), ROW_END },
		{ C("."), C(","), C("?"), C("!"), C("'"), C("+"), C("="), C("#"),
		  { "⌫", NULL, 1.4f, K_BACK, 0 }, ROW_END },
		{ { "ABC", NULL, 1.5f, K_PAGE, PAGE_ABC }, { "àè", NULL, 1.2f, K_PAGE, PAGE_ACC },
		  { "spazio", " ", 4.6f, K_SPACE, 0 }, { "Invia", NULL, 2.2f, K_ENTER, 0 }, ROW_END },
	},
	[PAGE_ACC] = {
		{ C("à"), C("è"), C("é"), C("ì"), C("ò"), C("ù"), C("È"), C("ç"), C("°"), C("%"), ROW_END },
		{ { ":)", ":) ", 1.6f, K_CHAR, 0 }, { ":D", ":D ", 1.6f, K_CHAR, 0 },
		  { ";)", ";) ", 1.6f, K_CHAR, 0 }, { ":(", ":( ", 1.6f, K_CHAR, 0 },
		  { "<3", "<3 ", 1.6f, K_CHAR, 0 }, ROW_END },
		{ { "ciao!", "ciao! ", 2.4f, K_CHAR, 0 }, { "ok", "ok ", 1.6f, K_CHAR, 0 },
		  { "grazie", "grazie ", 2.6f, K_CHAR, 0 }, { "⌫", NULL, 1.4f, K_BACK, 0 }, ROW_END },
		{ { "ABC", NULL, 1.5f, K_PAGE, PAGE_ABC }, { "123", NULL, 1.2f, K_PAGE, PAGE_123 },
		  { "spazio", " ", 4.6f, K_SPACE, 0 }, { "Invia", NULL, 2.2f, K_ENTER, 0 }, ROW_END },
	},
};

static int page, shift;
static int pressed_r = -1, pressed_c = -1;

#define KB_KEY_H   P(42)
#define ROW_GAP P(11)
#define TOP_PAD P(8)

int kb_height(void) { return TOP_PAD + 4 * KB_KEY_H + 3 * ROW_GAP + P(8); }
int kb_top(void) { return G.h - P(30) - kb_height(); }
void kb_set_shift(int on) { if (page == PAGE_ABC) shift = on; }

/* Rettangolo del tasto (r, c). Ritorna 0 se non esiste. */
static int key_rect(int r, int c, int *x, int *y, int *w, int *h)
{
	const struct key *row = pages[page][r];
	float total = 0;
	int n = 0;
	while (row[n].label) {
		total += row[n].w;
		n++;
	}
	if (c >= n)
		return 0;
	int margin = P(3), gap = P(6);
	float unit = (G.w - 2 * margin - 9 * gap) / 10.0f;
	/* le righe con tasti larghi riempiono tutta la larghezza */
	if (total > 10)
		unit = (G.w - 2 * margin - (n - 1) * gap) / total;
	float roww = total * unit + (n - 1) * gap;
	float kx = (G.w - roww) / 2;
	for (int i = 0; i < c; i++)
		kx += row[i].w * unit + gap;
	*x = (int)kx;
	*w = (int)(row[c].w * unit);
	*y = kb_top() + TOP_PAD + r * (KB_KEY_H + ROW_GAP);
	*h = KB_KEY_H;
	return 1;
}

static const char *label_of(const struct key *k, char *buf, int n)
{
	if (k->kind == K_CHAR && shift && page == PAGE_ABC && k->out[1] == 0 &&
	    k->out[0] >= 'a' && k->out[0] <= 'z') {
		snprintf(buf, n, "%c", k->out[0] - 32);
		return buf;
	}
	return k->label;
}

void kb_draw(void)
{
	int top = kb_top();
	gfx_rect(0, top, G.w, G.h - top, RGB(209, 212, 219), 255);
	for (int r = 0; r < 4; r++)
		for (int c = 0; pages[page][r][c].label; c++) {
			const struct key *k = &pages[page][r][c];
			int x, y, w, h;
			char buf[8];
			key_rect(r, c, &x, &y, &w, &h);
			int special = k->kind != K_CHAR && k->kind != K_SPACE;
			color_t bg = special ? RGB(172, 177, 188) : WHITE;
			if (k->kind == K_ENTER)
				bg = RGB(0, 122, 255);
			if (k->kind == K_SHIFT && shift)
				bg = WHITE;
			if (r == pressed_r && c == pressed_c)
				bg = special ? WHITE : RGB(172, 177, 188);
			gfx_rrect(x, y + P(1), w, h, P(6), RGB(136, 138, 142), 255); /* ombra */
			gfx_rrect(x, y, w, h, P(6), bg, 255);
			const char *lab = label_of(k, buf, sizeof(buf));
			int fs = k->kind == K_CHAR && strlen(k->label) <= 2 ? P(22) : P(16);
			color_t fg = k->kind == K_ENTER ? WHITE : BLACK;
			text_center(x + w / 2, y + (h - fs) / 2 - P(2), lab, fs, FONT_REGULAR, fg, 255);
		}
	/* fumetto sopra il tasto premuto */
	if (pressed_r >= 0) {
		const struct key *k = &pages[page][pressed_r][pressed_c];
		int x, y, w, h;
		char buf[8];
		if (k->kind == K_CHAR && key_rect(pressed_r, pressed_c, &x, &y, &w, &h) && k->w <= 1) {
			int bw = w + P(16), bh = P(54), bx = x - P(8), by = y - bh - P(4);
			gfx_shadow(bx, by, bw, bh + h, P(10), P(4), 60);
			gfx_rrect(bx, by, bw, bh + P(8), P(10), WHITE, 255);
			gfx_rect(x, by + bh, w, h + P(4) - P(4), WHITE, 255);
			text_center(x + w / 2, by + P(8), label_of(k, buf, sizeof(buf)), P(32),
				    FONT_REGULAR, BLACK, 255);
		}
	}
}

static int hit(int px, int py, int *rr, int *cc)
{
	for (int r = 0; r < 4; r++)
		for (int c = 0; pages[page][r][c].label; c++) {
			int x, y, w, h;
			key_rect(r, c, &x, &y, &w, &h);
			/* area di tocco allargata a metà degli spazi */
			if (px >= x - P(3) && px < x + w + P(3) && py >= y - ROW_GAP / 2 &&
			    py < y + h + ROW_GAP / 2) {
				*rr = r;
				*cc = c;
				return 1;
			}
		}
	return 0;
}

enum kb_action kb_event(const struct event *ev, char *out, int n)
{
	int r, c;

	if (ev->type == IN_TOUCH_DOWN || ev->type == IN_TOUCH_MOVE) {
		if (hit(ev->x, ev->y, &r, &c)) {
			pressed_r = r;
			pressed_c = c;
		} else {
			pressed_r = pressed_c = -1;
		}
		ui_redraw();
		return KB_NONE;
	}
	if (ev->type != IN_TOUCH_UP || pressed_r < 0)
		return KB_NONE;
	const struct key *k = &pages[page][pressed_r][pressed_c];
	pressed_r = pressed_c = -1;
	ui_redraw();
	switch (k->kind) {
	case K_SHIFT:
		shift = !shift;
		return KB_NONE;
	case K_PAGE:
		page = k->page;
		shift = 0;
		return KB_NONE;
	case K_BACK:
		return KB_BACKSPACE;
	case K_ENTER:
		return KB_ENTER;
	case K_SPACE:
	case K_CHAR: {
		char buf[8];
		snprintf(out, n, "%s", k->kind == K_CHAR ? label_of(k, buf, sizeof(buf)) : " ");
		if (k->kind == K_CHAR && strlen(k->out) > strlen(out))
			snprintf(out, n, "%s", k->out); /* scorciatoie tipo "ciao! " */
		if (shift)
			shift = 0;
		/* dopo uno spazio nella pagina dei numeri torna alle lettere */
		if (k->kind == K_SPACE && page != PAGE_ABC)
			page = PAGE_ABC;
		return KB_TEXT;
	}
	}
	return KB_NONE;
}

/* ---- tastiera fisica (utile nell'emulatore) ---- */

static int phys_shift;

enum kb_action kb_physical(const struct event *ev, char *out, int n)
{
	static const char lower[] = {
		[KEY_A] = 'a', [KEY_B] = 'b', [KEY_C] = 'c', [KEY_D] = 'd', [KEY_E] = 'e',
		[KEY_F] = 'f', [KEY_G] = 'g', [KEY_H] = 'h', [KEY_I] = 'i', [KEY_J] = 'j',
		[KEY_K] = 'k', [KEY_L] = 'l', [KEY_M] = 'm', [KEY_N] = 'n', [KEY_O] = 'o',
		[KEY_P] = 'p', [KEY_Q] = 'q', [KEY_R] = 'r', [KEY_S] = 's', [KEY_T] = 't',
		[KEY_U] = 'u', [KEY_V] = 'v', [KEY_W] = 'w', [KEY_X] = 'x', [KEY_Y] = 'y',
		[KEY_Z] = 'z', [KEY_1] = '1', [KEY_2] = '2', [KEY_3] = '3', [KEY_4] = '4',
		[KEY_5] = '5', [KEY_6] = '6', [KEY_7] = '7', [KEY_8] = '8', [KEY_9] = '9',
		[KEY_0] = '0', [KEY_SPACE] = ' ', [KEY_DOT] = '.', [KEY_COMMA] = ',',
		[KEY_MINUS] = '-', [KEY_SLASH] = '/', [KEY_APOSTROPHE] = '\'', [KEY_SEMICOLON] = ';',
		[KEY_EQUAL] = '=',
	};
	static const char upper_sym[] = {
		[KEY_1] = '!', [KEY_2] = '@', [KEY_3] = '#', [KEY_4] = '$', [KEY_5] = '%',
		[KEY_7] = '&', [KEY_8] = '*', [KEY_9] = '(', [KEY_0] = ')', [KEY_SLASH] = '?',
		[KEY_SEMICOLON] = ':', [KEY_APOSTROPHE] = '"', [KEY_MINUS] = '_', [KEY_EQUAL] = '+',
		[KEY_DOT] = '>', [KEY_COMMA] = '<',
	};

	if (ev->code == KEY_LEFTSHIFT || ev->code == KEY_RIGHTSHIFT) {
		phys_shift = ev->type != IN_KEY_UP;
		return KB_NONE;
	}
	if (ev->type == IN_KEY_UP)
		return KB_NONE;
	if (ev->code == KEY_BACKSPACE)
		return KB_BACKSPACE;
	if (ev->type != IN_KEY)
		return KB_NONE;
	if (ev->code == KEY_ENTER || ev->code == KEY_KPENTER)
		return KB_ENTER;
	if (ev->code >= (int)sizeof(lower) || !lower[ev->code])
		return KB_NONE;
	char ch = lower[ev->code];
	if (phys_shift) {
		if (ch >= 'a' && ch <= 'z')
			ch -= 32;
		else if (ev->code < (int)sizeof(upper_sym) && upper_sym[ev->code])
			ch = upper_sym[ev->code];
	}
	snprintf(out, n, "%c", ch);
	return KB_TEXT;
}
