/* OSWiko - Messaggi: chat con una persona tramite il servizio msgd.
 *
 * Legge i messaggi da /data/msg/log, scrive quelli da inviare in
 * /data/msg/outbox. Si aggiorna da sola grazie a inotify, anche quando
 * l'app è chiusa (badge sull'icona e banner di notifica).
 */
#include "keyboard.h"
#include "msg.h"
#include "ui.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_MSG   400
#define MAX_LINES 40
#define BLUE      RGB(0, 122, 255)
#define GRAYB     RGB(233, 233, 235)
#define SUBTLE    RGB(142, 142, 147)

struct msg {
	char id[24];
	long ts;
	char dir;          /* 'I' ricevuto, 'O' inviato */
	int pending;       /* scritto ma non ancora inviato */
	char *text;
	/* impaginazione (calcolata una volta) */
	int nlines, bw, bh;
	short start[MAX_LINES], len[MAX_LINES];
};

static struct msg msgs[MAX_MSG];
static int n_msgs, total_in, seen_in;
static char status[16], code[8], name[64] = "Telegram";
static char input[1024];
static int focused, scroll, content_h;
static int drag_y0 = -1, drag_scroll0, dragged;

/* ---------------- testo a capo ---------------- */

static int FS, LH;   /* dimensione font e altezza riga */

static int wsub(const char *s, int a, int b)
{
	char tmp[2048];
	int n = b - a < (int)sizeof(tmp) - 1 ? b - a : (int)sizeof(tmp) - 1;
	memcpy(tmp, s + a, n);
	tmp[n] = 0;
	return text_width(tmp, FS, FONT_REGULAR);
}

/* Divide s in righe larghe al massimo maxw pixel. */
static int wrap(const char *s, int maxw, short *start, short *len, int maxl)
{
	int n = 0, L = strlen(s), ls = 0, le = 0, k = 0;

	while (n < maxl) {
		int we = k;
		while (we < L && s[we] != ' ' && s[we] != '\n')
			we++;
		if (wsub(s, ls, we) <= maxw) {
			le = we;
			k = we;
			if (k < L && s[k] == ' ') {
				k++;
				continue;
			}
			start[n] = ls; len[n] = le - ls; n++;
			if (k >= L)
				break;
			ls = le = k = k + 1; /* dopo il "a capo" */
			continue;
		}
		if (le > ls) {
			start[n] = ls; len[n] = le - ls; n++;
			ls = le = k = (s[le] == ' ') ? le + 1 : le;
			continue;
		}
		/* parola più lunga della riga: spezzala carattere per carattere */
		int e = ls + 1;
		while (e < we) {
			int nx = e + 1;
			while (nx < we && (s[nx] & 0xc0) == 0x80)
				nx++;
			if (wsub(s, ls, nx) > maxw)
				break;
			e = nx;
		}
		start[n] = ls; len[n] = e - ls; n++;
		ls = le = k = e;
	}
	return n ? n : 1;
}

static void layout_msg(struct msg *m)
{
	int maxw = G.w * 70 / 100 - P(28);
	m->nlines = wrap(m->text, maxw, m->start, m->len, MAX_LINES);
	if (m->text[0] == 0) {
		m->nlines = 1;
		m->start[0] = 0;
		m->len[0] = 0;
	}
	int w = 0;
	for (int i = 0; i < m->nlines; i++) {
		int lw = wsub(m->text, m->start[i], m->start[i] + m->len[i]);
		if (lw > w)
			w = lw;
	}
	m->bw = w + P(28);
	m->bh = m->nlines * LH + P(16);
}

/* ---------------- dati ---------------- */

static void read_state(void)
{
	FILE *f = fopen(MSG_STATE, "r");
	char line[128];
	if (!f) {
		snprintf(status, sizeof(status), "start");
		return;
	}
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = 0;
		if (!strncmp(line, "status=", 7)) snprintf(status, sizeof(status), "%.15s", line + 7);
		else if (!strncmp(line, "code=", 5)) snprintf(code, sizeof(code), "%.7s", line + 5);
		else if (!strncmp(line, "name=", 5) && line[5]) snprintf(name, sizeof(name), "%.63s", line + 5);
	}
	fclose(f);
}

static void add_msg(const char *id, long ts, char dir, int pending, const char *esc)
{
	if (n_msgs == MAX_MSG) { /* tieni solo gli ultimi */
		free(msgs[0].text);
		memmove(msgs, msgs + 1, sizeof(msgs[0]) * (MAX_MSG - 1));
		n_msgs--;
	}
	struct msg *m = &msgs[n_msgs++];
	memset(m, 0, sizeof(*m));
	snprintf(m->id, sizeof(m->id), "%s", id);
	m->ts = ts;
	m->dir = dir;
	m->pending = pending;
	char text[8192];
	msg_unescape(esc, text, sizeof(text));
	m->text = strdup(text);
	layout_msg(m);
}

static int sent_has(const char *id)
{
	for (int i = 0; i < n_msgs; i++)
		if (msgs[i].dir == 'O' && !strcmp(msgs[i].id, id))
			return 1;
	return 0;
}

static void load(void)
{
	char line[8400];
	FS = P(16);
	LH = P(21);
	for (int i = 0; i < n_msgs; i++)
		free(msgs[i].text);
	n_msgs = 0;
	total_in = 0;
	read_state();

	FILE *f = fopen(MSG_LOG, "r");
	while (f && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = 0;
		char *id = strtok(line, "\t"), *ts = strtok(NULL, "\t"), *dir = strtok(NULL, "\t");
		char *text = strtok(NULL, "");
		if (!id || !ts || !dir)
			continue;
		add_msg(id, atol(ts), dir[0], 0, text ? text : "");
		if (dir[0] == 'I')
			total_in++;
	}
	if (f)
		fclose(f);
	/* messaggi in uscita non ancora confermati */
	f = fopen(MSG_OUTBOX, "r");
	while (f && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = 0;
		char *tab = strchr(line, '\t');
		if (!tab)
			continue;
		*tab = 0;
		if (!sent_has(line))
			add_msg(line, atoll(line) / 1000, 'O', 1, tab + 1);
	}
	if (f)
		fclose(f);
}

static void save_seen(void)
{
	FILE *f = fopen(MSG_DIR "/seen", "w");
	if (f) {
		fprintf(f, "%d\n", seen_in);
		fclose(f);
	}
}

static void on_change(int fd)
{
	char buf[4096];
	while (read(fd, buf, sizeof(buf)) > 0)
		; /* svuota gli eventi */
	int old = total_in;
	load();
	if (total_in > old) {
		if (ui_is_open(&app_chat)) {
			seen_in = total_in;
			save_seen();
		} else {
			for (int i = n_msgs - 1; i >= 0; i--)
				if (msgs[i].dir == 'I') {
					ui_notify(&app_chat, name, msgs[i].text);
					break;
				}
		}
	}
	app_chat.badge = total_in - seen_in > 0 ? total_in - seen_in : 0;
	ui_badges_changed();
	ui_redraw();
}

static void init(void)
{
	mkdir("/data", 0755);
	mkdir(MSG_DIR, 0755);
	mkdir(MSG_RUN, 0755);
	int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd >= 0) {
		inotify_add_watch(fd, MSG_DIR, IN_CLOSE_WRITE | IN_MODIFY | IN_CREATE);
		inotify_add_watch(fd, MSG_RUN, IN_MOVED_TO | IN_CLOSE_WRITE);
		ui_watch_fd(fd, on_change);
	}
	FILE *f = fopen(MSG_DIR "/seen", "r");
	if (f) {
		if (fscanf(f, "%d", &seen_in) != 1)
			seen_in = 0;
		fclose(f);
	}
	load();
	app_chat.badge = total_in - seen_in > 0 ? total_in - seen_in : 0;
}

static void open_(void)
{
	load();
	seen_in = total_in;
	save_seen();
	app_chat.badge = 0;
	scroll = 0;
	focused = 0;
}

static int paired(void)
{
	return strcmp(status, "pair") && strcmp(status, "notoken") &&
	       strcmp(status, "badtoken") && strcmp(status, "start");
}

static void send_input(void)
{
	char *s = input;
	while (*s == ' ' || *s == '\n')
		s++;
	if (!*s)
		return;
	char esc[2100], line[2200];
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	msg_escape(s, esc, sizeof(esc));
	int n = snprintf(line, sizeof(line), "%lld\t%s\n",
			 ts.tv_sec * 1000LL + ts.tv_nsec / 1000000, esc);
	int fd = open(MSG_OUTBOX, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd >= 0) {
		if (write(fd, line, n) < 0)
			perror("chat: outbox");
		close(fd);
	}
	input[0] = 0;
	scroll = 0;
	kb_set_shift(1);
	load();
	ui_redraw();
}

/* ---------------- disegno ---------------- */

static int header_bottom(void) { return P(102); }

static void field_geom(int *fx, int *fw, int *fh, int *nl, short *st, short *ln)
{
	*fx = P(12);
	*fw = G.w - P(12) - P(56);
	FS = P(16);
	LH = P(21);
	*nl = input[0] ? wrap(input, *fw - P(28), st, ln, 12) : 1;
	int shown = *nl > 4 ? 4 : *nl;
	*fh = shown * LH + P(16);
}

static int bar_top(void)
{
	int fx, fw, fh, nl;
	short st[12], ln[12];
	field_geom(&fx, &fw, &fh, &nl, st, ln);
	int bottom = focused ? kb_top() : G.h - P(34);
	return bottom - fh - P(16);
}

static void time_label(long ts, char *buf, int n)
{
	static const char *mesi[] = { "gen", "feb", "mar", "apr", "mag", "giu",
				      "lug", "ago", "set", "ott", "nov", "dic" };
	time_t t = ts, now = time(NULL);
	struct tm tm, tn;
	localtime_r(&t, &tm);
	localtime_r(&now, &tn);
	if (tm.tm_yday == tn.tm_yday && tm.tm_year == tn.tm_year)
		snprintf(buf, n, "Oggi %02d:%02d", tm.tm_hour, tm.tm_min);
	else
		snprintf(buf, n, "%d %s %02d:%02d", tm.tm_mday, mesi[tm.tm_mon], tm.tm_hour, tm.tm_min);
}

static void draw_card(int top, int bottom, const char *title, const char **lines, int n,
		      const char *big)
{
	int w = G.w - P(48), x = P(24);
	int h = P(70) + n * P(24) + (big ? P(90) : 0);
	int y = top + (bottom - top - h) / 2;
	gfx_rrect(x, y, w, h, P(20), RGB(242, 242, 247), 255);
	text_center(G.w / 2, y + P(22), title, P(19), FONT_SEMIBOLD, BLACK, 255);
	int ty = y + P(60);
	if (big) {
		text_center(G.w / 2, ty, big, P(64), FONT_SEMIBOLD, BLUE, 255);
		ty += P(90);
	}
	for (int i = 0; i < n; i++)
		text_center(G.w / 2, ty + i * P(24), lines[i], P(14.5f), FONT_REGULAR, RGB(60, 60, 67), 255);
}

static void draw(void)
{
	int hb = header_bottom(), bt = paired() ? bar_top() : G.h - P(34);
	char buf[64];

	gfx_clear(WHITE);

	/* ---- messaggi ---- */
	int y = 0;
	long prev_ts = 0;
	char prev_dir = 0;
	/* prima passata: altezza totale */
	for (int i = 0; i < n_msgs; i++) {
		if (msgs[i].ts - prev_ts > 15 * 60)
			y += P(34);
		else if (msgs[i].dir != prev_dir)
			y += P(8);
		else
			y += P(3);
		y += msgs[i].bh + (msgs[i].pending ? P(16) : 0);
		prev_ts = msgs[i].ts;
		prev_dir = msgs[i].dir;
	}
	content_h = y;
	int area = bt - hb - P(16);
	int max_scroll = content_h > area ? content_h - area : 0;
	if (scroll > max_scroll)
		scroll = max_scroll;
	if (scroll < 0)
		scroll = 0;
	int base = bt - P(8) - content_h + scroll; /* y sullo schermo del contenuto */
	if (content_h < area)
		base = hb + P(8);

	y = 0;
	prev_ts = 0;
	prev_dir = 0;
	for (int i = 0; i < n_msgs; i++) {
		struct msg *m = &msgs[i];
		if (m->ts - prev_ts > 15 * 60) {
			y += P(34);
			if (base + y - P(26) > hb - P(30) && base + y < bt) {
				time_label(m->ts, buf, sizeof(buf));
				text_center(G.w / 2, base + y - P(26), buf, P(12), FONT_SEMIBOLD, SUBTLE, 255);
			}
		} else if (m->dir != prev_dir) {
			y += P(8);
		} else {
			y += P(3);
		}
		int sy = base + y;
		if (sy + m->bh > hb - P(40) && sy < bt) {
			int out = m->dir == 'O';
			int bx = out ? G.w - P(12) - m->bw : P(12);
			int r = m->bh / 2 < P(18) ? m->bh / 2 : P(18);
			gfx_rrect(bx, sy, m->bw, m->bh, r, out ? BLUE : GRAYB, m->pending ? 140 : 255);
			for (int l = 0; l < m->nlines; l++) {
				char line[2048];
				int n = m->len[l] < (int)sizeof(line) - 1 ? m->len[l] : (int)sizeof(line) - 1;
				memcpy(line, m->text + m->start[l], n);
				line[n] = 0;
				text_draw(bx + P(14), sy + P(8) + l * LH - P(1), line, FS, FONT_REGULAR,
					  out ? WHITE : BLACK, 255);
			}
			if (m->pending)
				text_right(G.w - P(16), sy + m->bh + P(2), "invio…", P(11), FONT_REGULAR, SUBTLE, 255);
		}
		y += m->bh + (m->pending ? P(16) : 0);
		prev_ts = m->ts;
		prev_dir = m->dir;
	}

	/* ---- stati speciali ---- */
	if (!strcmp(status, "notoken") || !strcmp(status, "start")) {
		const char *l[] = { "1. Su Telegram cerca @BotFather", "e crea un bot con /newbot",
				    "2. Metti il token nel file telegram.conf", "3. Riavvia OSWiko" };
		draw_card(hb, bt, "Messaggi da configurare", l, 4, NULL);
	} else if (!strcmp(status, "badtoken")) {
		const char *l[] = { "Telegram non riconosce il token.", "Controlla il file telegram.conf." };
		draw_card(hb, bt, "Token del bot non valido", l, 2, NULL);
	} else if (!strcmp(status, "pair")) {
		const char *l[] = { "Su Telegram, scrivi questo codice", "al tuo bot per collegarti." };
		draw_card(hb, bt, "Collega il telefono", l, 2, code);
	} else if (n_msgs == 0) {
		text_center(G.w / 2, (hb + bt) / 2 - P(10), "Nessun messaggio. Scrivi qualcosa!",
			    P(15), FONT_REGULAR, SUBTLE, 255);
	}

	/* ---- intestazione ---- */
	gfx_rect(0, 0, G.w, hb, RGB(249, 249, 250), 250);
	gfx_rect(0, hb, G.w, 1, RGB(210, 210, 214), 255);
	int cx = G.w / 2, ay = P(48);
	gfx_circle(cx, ay + P(17), P(17), RGB(160, 165, 178), 255);
	char ini[8] = "?";
	if (name[0]) {
		int n = 1;
		while (name[n] && (name[n] & 0xc0) == 0x80)
			n++;
		memcpy(ini, name, n);
		ini[n] = 0;
	}
	text_center(cx, ay + P(6), ini, P(18), FONT_SEMIBOLD, WHITE, 255);
	char nm[80];
	text_fit(name, P(13), FONT_SEMIBOLD, G.w - P(140), nm, sizeof(nm));
	text_center(cx, ay + P(36), nm, P(13), FONT_SEMIBOLD, BLACK, 255);
	const char *sub = !strcmp(status, "ok") ? "online" :
			  !strcmp(status, "offline") ? "non in linea, riprovo…" :
			  !strcmp(status, "connecting") ? "connessione…" :
			  !strcmp(status, "pair") ? "da collegare" : "";
	/* freccia indietro */
	gfx_line(P(22), ay + P(17), P(31), ay + P(8), P(3), BLUE, 255);
	gfx_line(P(22), ay + P(17), P(31), ay + P(26), P(3), BLUE, 255);
	if (sub[0]) {
		color_t dot = !strcmp(status, "ok") ? RGB(52, 199, 89) : SUBTLE;
		gfx_circle(G.w - P(24), ay + P(17), P(4.5f), dot, 255);
	}

	/* ---- barra di scrittura ---- */
	if (paired()) {
		int fx, fw, fh, nl;
		short st[12], ln[12];
		field_geom(&fx, &fw, &fh, &nl, st, ln);
		gfx_rect(0, bt, G.w, (focused ? kb_top() : G.h) - bt, RGB(249, 249, 250), 255);
		int fy = bt + P(8);
		gfx_rrect(fx, fy, fw, fh, P(18), RGB(200, 200, 205), 255);
		gfx_rrect(fx + 1, fy + 1, fw - 2, fh - 2, P(18) - 1, WHITE, 255);
		if (!input[0]) {
			text_draw(fx + P(14), fy + P(8) - P(1), "Messaggio", FS, FONT_REGULAR, SUBTLE, 255);
		} else {
			int first = nl > 4 ? nl - 4 : 0; /* mostra le ultime 4 righe */
			for (int l = first; l < nl; l++) {
				char line[1100];
				memcpy(line, input + st[l], ln[l]);
				line[ln[l]] = 0;
				text_draw(fx + P(14), fy + P(8) + (l - first) * LH - P(1), line, FS,
					  FONT_REGULAR, BLACK, 255);
			}
		}
		if (focused) { /* cursore */
			int last = nl - 1;
			char line[1100];
			int n = input[0] ? ln[last] : 0;
			memcpy(line, input + (input[0] ? st[last] : 0), n);
			line[n] = 0;
			int cxp = fx + P(14) + text_width(line, FS, FONT_REGULAR) + P(1);
			int shown = nl > 4 ? 3 : nl - 1;
			if ((ui_now_ms() / 500) % 2 == 0)
				gfx_rect(cxp, fy + P(9) + shown * LH, P(2), LH - P(2), BLUE, 255);
		}
		/* pulsante invia */
		float bx = G.w - P(32), by = fy + fh - P(18);
		gfx_circle(bx, by, P(16), input[0] ? BLUE : RGB(200, 200, 205), 255);
		gfx_line(bx, by + P(7), bx, by - P(7), P(3), WHITE, 255);
		gfx_line(bx, by - P(7), bx - P(6), by - P(1), P(3), WHITE, 255);
		gfx_line(bx, by - P(7), bx + P(6), by - P(1), P(3), WHITE, 255);
		if (focused)
			kb_draw();
	}
}

/* ---------------- input ---------------- */

static void backspace(void)
{
	int n = strlen(input);
	if (!n)
		return;
	do
		n--;
	while (n > 0 && (input[n] & 0xc0) == 0x80);
	input[n] = 0;
}

static void insert(const char *t)
{
	if (strlen(input) + strlen(t) < sizeof(input) - 1)
		strcat(input, t);
	int n = strlen(input);
	kb_set_shift(n >= 2 && (input[n - 2] == '.' || input[n - 2] == '!' ||
				input[n - 2] == '?') && input[n - 1] == ' ');
}

static void blink(void)
{
	if (focused) {
		ui_redraw();
		ui_timer(500);
	}
}

static void event(const struct event *ev)
{
	char txt[16];
	enum kb_action a;

	if (ev->type == IN_KEY || ev->type == IN_KEY_UP || ev->type == IN_KEY_REPEAT) {
		if (!paired())
			return;
		a = kb_physical(ev, txt, sizeof(txt));
		if (a == KB_TEXT) insert(txt);
		else if (a == KB_BACKSPACE) backspace();
		else if (a == KB_ENTER) send_input();
		if (a != KB_NONE)
			ui_redraw();
		return;
	}

	int hb = header_bottom();
	/* un gesto iniziato sulla tastiera va tutto alla tastiera */
	static int kb_touch;
	if (ev->type == IN_TOUCH_DOWN)
		kb_touch = focused && ev->y >= kb_top();
	if (kb_touch) {
		a = kb_event(ev, txt, sizeof(txt));
		if (a == KB_TEXT) insert(txt);
		else if (a == KB_BACKSPACE) backspace();
		else if (a == KB_ENTER) send_input();
		if (ev->type == IN_TOUCH_UP)
			kb_touch = 0;
		ui_redraw();
		return;
	}

	switch (ev->type) {
	case IN_TOUCH_DOWN:
		if (ev->y < hb && ev->x < P(70)) {
			ui_go_home();
			return;
		}
		if (paired() && ev->y >= bar_top() && (!focused || ev->y < kb_top())) {
			if (ev->x > G.w - P(56)) {
				send_input();
			} else if (!focused) {
				focused = 1;
				scroll = 0;
				kb_set_shift(input[0] == 0);
				ui_timer(500);
			}
			ui_redraw();
			return;
		}
		if (ev->y > hb) {
			drag_y0 = ev->y;
			drag_scroll0 = scroll;
			dragged = 0;
		}
		break;
	case IN_TOUCH_MOVE:
		if (drag_y0 >= 0) {
			if (abs(ev->y - drag_y0) > P(8))
				dragged = 1;
			if (dragged) {
				scroll = drag_scroll0 + (ev->y - drag_y0);
				ui_redraw();
			}
		}
		break;
	case IN_TOUCH_UP:
		if (drag_y0 >= 0 && !dragged && focused) {
			focused = 0; /* tocco sui messaggi: chiudi la tastiera */
			ui_redraw();
		}
		drag_y0 = -1;
		break;
	default:
		break;
	}
}

static void close_(void)
{
	focused = 0;
}

static void icon(int x, int y, int s)
{
	float cx = x + s / 2.0f, cy = y + s * 0.47f;
	/* fumetto: ellisse approssimata con un rettangolo molto arrotondato + coda */
	gfx_rrect((int)(cx - s * 0.32f), (int)(cy - s * 0.24f), (int)(s * 0.64f), (int)(s * 0.48f),
		  (int)(s * 0.24f), WHITE, 255);
	float tail[6] = { cx - s * 0.2f, cy + s * 0.12f, cx - s * 0.08f, cy + s * 0.2f,
			  cx - s * 0.27f, cy + s * 0.3f };
	gfx_poly(tail, 3, WHITE, 255);
}

struct app app_chat = {
	.name = "Messaggi",
	.icon_top = RGB(100, 220, 100), .icon_bottom = RGB(40, 180, 70),
	.icon = icon, .init = init, .open = open_, .draw = draw, .event = event,
	.timer = blink, .close = close_, .light = 1,
};
