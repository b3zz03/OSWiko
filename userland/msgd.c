/* OSWiko - msgd: servizio dei messaggi tramite un bot Telegram.
 *
 * Un thread riceve i messaggi con il "long polling" di Telegram (una
 * richiesta resta aperta finché non arriva qualcosa: niente consumi a vuoto),
 * l'altro invia quelli scritti dall'app. Accetta messaggi SOLO dalla chat
 * abbinata con il codice mostrato sullo schermo.
 */
#include "msg.h"
#include "net.h"

#define JSMN_STATIC
#include "third_party/jsmn.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static struct {
	char token[128];
	char name[64];
	char api_host[128];
	char api_ip[64];
	char ca[256];
	int api_port;
	long long chat_id;
} cfg;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char pair_code[8];
static const char *status = "start";

/* ---------------- configurazione e stato ---------------- */

static void parse_conf(FILE *f)
{
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		char *eq = strchr(line, '=');
		if (!eq || line[0] == '#')
			continue;
		*eq = 0;
		char *k = line, *v = eq + 1;
		v[strcspn(v, "\r\n")] = 0;
		while (*v == ' ') v++;
		if (!strcmp(k, "token")) snprintf(cfg.token, sizeof(cfg.token), "%s", v);
		else if (!strcmp(k, "name")) snprintf(cfg.name, sizeof(cfg.name), "%s", v);
		else if (!strcmp(k, "chat_id")) cfg.chat_id = atoll(v);
		else if (!strcmp(k, "api_host")) snprintf(cfg.api_host, sizeof(cfg.api_host), "%s", v);
		else if (!strcmp(k, "api_ip")) snprintf(cfg.api_ip, sizeof(cfg.api_ip), "%s", v);
		else if (!strcmp(k, "api_port")) cfg.api_port = atoi(v);
		else if (!strcmp(k, "ca")) snprintf(cfg.ca, sizeof(cfg.ca), "%s", v);
	}
}

static void load_conf(void)
{
	memset(&cfg, 0, sizeof(cfg));
	strcpy(cfg.api_host, "api.telegram.org");
	strcpy(cfg.ca, "/etc/ssl/cacert.pem");
	cfg.api_port = 443;
	/* prima la configurazione passata dall'emulatore, poi quella salvata */
	FILE *f = fopen(MSG_FWCFG "telegram.conf/raw", "r");
	if (f) {
		parse_conf(f);
		fclose(f);
	}
	f = fopen(MSG_CONF, "r");
	if (f) {
		parse_conf(f);
		fclose(f);
	}
	/* i file di /sys non dichiarano la dimensione: mbedTLS vuole un file
	 * normale, quindi copiamo il certificato in /run */
	if (!strncmp(cfg.ca, "/sys/", 5)) {
		FILE *in = fopen(cfg.ca, "r"), *out = fopen(MSG_RUN "/ca.pem", "w");
		char buf[4096];
		size_t n;
		while (in && out && (n = fread(buf, 1, sizeof(buf), in)) > 0)
			fwrite(buf, 1, n, out);
		if (in) fclose(in);
		if (out) fclose(out);
		snprintf(cfg.ca, sizeof(cfg.ca), "%s", MSG_RUN "/ca.pem");
	}
}

static void save_pairing(long long chat_id, const char *name)
{
	int exists = access(MSG_CONF, F_OK) == 0;
	FILE *f = fopen(MSG_CONF, "a");
	if (!f)
		return;
	if (!exists) /* il token era arrivato dall'emulatore: salviamolo */
		fprintf(f, "token=%s\n", cfg.token);
	fprintf(f, "chat_id=%lld\n", chat_id);
	if (name && name[0])
		fprintf(f, "name=%s\n", name);
	fclose(f);
}

static void write_state(void)
{
	FILE *f = fopen(MSG_STATE ".tmp", "w");
	if (!f)
		return;
	fprintf(f, "status=%s\ncode=%s\nname=%s\n", status, pair_code,
		cfg.name[0] ? cfg.name : "Telegram");
	fclose(f);
	rename(MSG_STATE ".tmp", MSG_STATE); /* atomico: l'app non legge mai a metà */
}

static void set_status(const char *s)
{
	if (strcmp(status, s) != 0) {
		status = s;
		write_state();
		fprintf(stderr, "msgd: stato %s\n", s);
	}
}

static void append_log(const char *id, long ts, char dir, const char *text)
{
	char esc[8192];
	msg_escape(text, esc, sizeof(esc));
	char line[8400];
	int n = snprintf(line, sizeof(line), "%s\t%ld\t%c\t%s\n", id, ts, dir, esc);
	int fd = open(MSG_LOG, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd >= 0) {
		/* una sola write: la riga arriva intera anche con due scrittori */
		if (write(fd, line, n) < 0)
			perror("msgd: log");
		fsync(fd); /* il messaggio è sul disco prima di mostrarlo */
		close(fd);
	}
}

/* ---------------- JSON ---------------- */

static int tok_eq(const char *js, const jsmntok_t *t, const char *s)
{
	return t->type == JSMN_STRING && (int)strlen(s) == t->end - t->start &&
	       strncmp(js + t->start, s, t->end - t->start) == 0;
}

/* Indice del token che segue (salta oggetti e array annidati). */
static int skip(const jsmntok_t *t, int i)
{
	int n = 1;
	while (n > 0) {
		n += t[i].size;
		n--;
		i++;
	}
	return i;
}

/* Valore della chiave 'key' dentro l'oggetto in posizione obj, o -1. */
static int find(const char *js, const jsmntok_t *t, int obj, const char *key)
{
	if (t[obj].type != JSMN_OBJECT)
		return -1;
	int i = obj + 1;
	for (int k = 0; k < t[obj].size; k++) {
		if (tok_eq(js, &t[i], key))
			return i + 1;
		i = skip(t, i + 1);
	}
	return -1;
}

static long long tok_ll(const char *js, const jsmntok_t *t)
{
	return atoll(js + t->start);
}

static void put_utf8(char **o, char *end, unsigned cp)
{
	char *p = *o;
	if (cp < 0x80 && p < end) *p++ = cp;
	else if (cp < 0x800 && p + 1 < end) { *p++ = 0xc0 | cp >> 6; *p++ = 0x80 | (cp & 0x3f); }
	else if (cp < 0x10000 && p + 2 < end) { *p++ = 0xe0 | cp >> 12; *p++ = 0x80 | ((cp >> 6) & 0x3f); *p++ = 0x80 | (cp & 0x3f); }
	else if (p + 3 < end) { *p++ = 0xf0 | cp >> 18; *p++ = 0x80 | ((cp >> 12) & 0x3f); *p++ = 0x80 | ((cp >> 6) & 0x3f); *p++ = 0x80 | (cp & 0x3f); }
	*o = p;
}

/* Copia una stringa JSON decodificando gli escape (\n, \uXXXX, ...). */
static void tok_str(const char *js, const jsmntok_t *t, char *out, int n)
{
	char *o = out, *end = out + n - 1;
	for (int i = t->start; i < t->end && o < end; i++) {
		if (js[i] != '\\') {
			*o++ = js[i];
			continue;
		}
		i++;
		switch (js[i]) {
		case 'n': *o++ = '\n'; break;
		case 't': *o++ = '\t'; break;
		case 'r': break;
		case 'u': {
			unsigned cp = strtoul((char[5]){ js[i + 1], js[i + 2], js[i + 3], js[i + 4], 0 }, NULL, 16);
			i += 4;
			if (cp >= 0xd800 && cp < 0xdc00 && js[i + 1] == '\\' && js[i + 2] == 'u') {
				unsigned lo = strtoul((char[5]){ js[i + 3], js[i + 4], js[i + 5], js[i + 6], 0 }, NULL, 16);
				cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
				i += 6;
			}
			put_utf8(&o, end, cp);
			break;
		}
		default: *o++ = js[i];
		}
	}
	*o = 0;
}

static void json_escape(const char *in, char *out, int n)
{
	int k = 0;
	for (; *in && k < n - 8; in++) {
		unsigned char c = *in;
		if (c == '"' || c == '\\') { out[k++] = '\\'; out[k++] = c; }
		else if (c == '\n') { out[k++] = '\\'; out[k++] = 'n'; }
		else if (c < 0x20) k += snprintf(out + k, n - k, "\\u%04x", c);
		else out[k++] = c;
	}
	out[k] = 0;
}

/* ---------------- Telegram ---------------- */

#define PHOTO_TAG "\x01photo:"
#define PHOTO_DIR "/data/photos"

static struct https_target target(void)
{
	struct https_target t = { cfg.api_host, cfg.api_ip[0] ? cfg.api_ip : NULL,
				  cfg.api_port, cfg.ca };
	return t;
}

static int api(const char *method, const char *body, int timeout, char **resp)
{
	char path[512];
	struct https_target t = target();
	snprintf(path, sizeof(path), "/bot%s/%s", cfg.token, method);
	return https_request(&t, body ? "POST" : "GET", path, body, body ? strlen(body) : 0,
			     NULL, timeout, resp, NULL);
}

/* Invia una foto (upload "multipart/form-data", come un modulo web). */
static int send_photo(long long chat, const char *file)
{
	FILE *f = fopen(file, "rb");
	if (!f)
		return 200; /* foto cancellata nel frattempo: non riprovare all'infinito */
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	const char *B = "OSWikoConfine7a9c";
	char head[512], tail[64];
	int hl = snprintf(head, sizeof(head),
			  "--%s\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n%lld\r\n"
			  "--%s\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"foto.jpg\"\r\n"
			  "Content-Type: image/jpeg\r\n\r\n", B, chat, B);
	int tl = snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", B);
	char *body = malloc(hl + n + tl);
	if (!body) {
		fclose(f);
		return -1;
	}
	memcpy(body, head, hl);
	size_t got = fread(body + hl, 1, n, f);
	fclose(f);
	memcpy(body + hl + got, tail, tl);
	char path[256], ctype[96], *resp = NULL;
	snprintf(path, sizeof(path), "/bot%s/sendPhoto", cfg.token);
	snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", B);
	struct https_target t = target();
	int st = https_request(&t, "POST", path, body, hl + got + tl, ctype, 90, &resp, NULL);
	free(body);
	free(resp);
	return st;
}

/* Scarica una foto ricevuta e la salva nella Galleria. 0 = ok */
static int download_photo(const char *file_id, const char *dest)
{
	char method[300], *resp = NULL, fpath[256] = "";
	snprintf(method, sizeof(method), "getFile?file_id=%s", file_id);
	if (api(method, NULL, 30, &resp) != 200 || !resp) {
		free(resp);
		return -1;
	}
	jsmn_parser p;
	jsmntok_t t[64];
	jsmn_init(&p);
	if (jsmn_parse(&p, resp, strlen(resp), t, 64) > 0) {
		int r = find(resp, t, 0, "result");
		int fp = r >= 0 ? find(resp, t, r, "file_path") : -1;
		if (fp >= 0)
			tok_str(resp, &t[fp], fpath, sizeof(fpath));
	}
	free(resp);
	if (!fpath[0])
		return -1;

	char path[512];
	size_t len = 0;
	struct https_target tg = target();
	snprintf(path, sizeof(path), "/file/bot%s/%s", cfg.token, fpath);
	if (https_request(&tg, "GET", path, NULL, 0, NULL, 60, &resp, &len) != 200 || !resp) {
		free(resp);
		return -1;
	}
	mkdir(PHOTO_DIR, 0755);
	FILE *f = fopen(dest, "wb");
	int ok = f && fwrite(resp, 1, len, f) == len;
	if (f) {
		fflush(f);
		fsync(fileno(f));
		fclose(f);
	}
	free(resp);
	return ok ? 0 : -1;
}

static int send_text(long long chat, const char *text)
{
	char esc[8192], body[8400], *resp = NULL;
	json_escape(text, esc, sizeof(esc));
	snprintf(body, sizeof(body), "{\"chat_id\":%lld,\"text\":\"%s\"}", chat, esc);
	int st = api("sendMessage", body, 30, &resp);
	free(resp);
	return st;
}

static long long load_offset(void)
{
	long long off = 0;
	FILE *f = fopen(MSG_DIR "/offset", "r");
	if (f) {
		if (fscanf(f, "%lld", &off) != 1)
			off = 0;
		fclose(f);
	}
	return off;
}

static void save_offset(long long off)
{
	FILE *f = fopen(MSG_DIR "/offset", "w");
	if (f) {
		fprintf(f, "%lld\n", off);
		fclose(f);
	}
}

static void handle_message(const char *js, jsmntok_t *t, int m)
{
	int ichat = find(js, t, m, "chat");
	int iid = ichat >= 0 ? find(js, t, ichat, "id") : -1;
	if (iid < 0)
		return;
	long long chat = tok_ll(js, &t[iid]);
	char text[4096] = "";
	int itext = find(js, t, m, "text");
	int idate = find(js, t, m, "date");
	int imid = find(js, t, m, "message_id");
	long date = idate >= 0 ? (long)tok_ll(js, &t[idate]) : time(NULL);

	int iphoto = find(js, t, m, "photo");
	char photo_file[160] = "";
	if (iphoto >= 0 && t[iphoto].type == JSMN_ARRAY) {
		/* Telegram manda più dimensioni: prendi la più grande fino a 1280 px */
		char best_id[256] = "";
		int best_w = 0, i = iphoto + 1;
		for (int k = 0; k < t[iphoto].size; k++) {
			int iw = find(js, t, i, "width"), ifid = find(js, t, i, "file_id");
			int w = iw >= 0 ? (int)tok_ll(js, &t[iw]) : 0;
			if (ifid >= 0 && (best_id[0] == 0 || (w > best_w && w <= 1280))) {
				tok_str(js, &t[ifid], best_id, sizeof(best_id));
				best_w = w;
			}
			i = skip(t, i);
		}
		snprintf(photo_file, sizeof(photo_file), PHOTO_DIR "/TG_%lld.jpg",
			 imid >= 0 ? tok_ll(js, &t[imid]) : (long long)time(NULL));
		if (best_id[0] && download_photo(best_id, photo_file) == 0)
			snprintf(text, sizeof(text), PHOTO_TAG "%s", photo_file);
		else
			snprintf(text, sizeof(text), "[Foto non scaricata]");
	} else if (itext >= 0) {
		tok_str(js, &t[itext], text, sizeof(text));
	} else {
		/* contenuti che non sappiamo ancora mostrare */
		static const struct { const char *key, *label; } kinds[] = {
			{ "photo", "📷 Foto" }, { "sticker", "Sticker" }, { "voice", "🎤 Messaggio vocale" },
			{ "video", "Video" }, { "document", "File" }, { "audio", "Audio" },
			{ "location", "Posizione" }, { "animation", "GIF" },
		};
		snprintf(text, sizeof(text), "[messaggio non supportato]");
		for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++)
			if (find(js, t, m, kinds[k].key) >= 0)
				snprintf(text, sizeof(text), "[%s]", kinds[k].label);
	}

	pthread_mutex_lock(&lock);
	long long paired = cfg.chat_id;
	pthread_mutex_unlock(&lock);

	if (!paired) {
		/* abbinamento: il messaggio deve contenere il codice sullo schermo */
		if (strstr(text, pair_code)) {
			char name[64] = "";
			int ifrom = find(js, t, m, "from");
			int ifn = ifrom >= 0 ? find(js, t, ifrom, "first_name") : -1;
			if (ifn >= 0)
				tok_str(js, &t[ifn], name, sizeof(name));
			pthread_mutex_lock(&lock);
			cfg.chat_id = chat;
			if (!cfg.name[0] && name[0])
				snprintf(cfg.name, sizeof(cfg.name), "%s", name);
			pthread_mutex_unlock(&lock);
			save_pairing(chat, cfg.name);
			fprintf(stderr, "msgd: abbinato alla chat %lld (%s)\n", chat, cfg.name);
			send_text(chat, "✅ Collegato a OSWiko! Ora puoi scrivere qui.");
			status = "";
			set_status("ok");
		} else {
			send_text(chat, "Per collegarti scrivi il codice che vedi nell'app Messaggi di OSWiko.");
		}
		return;
	}
	if (chat != paired)
		return; /* sconosciuto: ignora */
	char id[32];
	snprintf(id, sizeof(id), "t%lld", imid >= 0 ? tok_ll(js, &t[imid]) : 0);
	append_log(id, date, 'I', text);
	/* didascalia della foto: come messaggio separato */
	int icap = find(js, t, m, "caption");
	if (iphoto >= 0 && icap >= 0) {
		tok_str(js, &t[icap], text, sizeof(text));
		strcat(id, "c");
		append_log(id, date, 'I', text);
	}
}

static void *receiver(void *arg)
{
	long long offset = load_offset();
	int backoff = 2;
	(void)arg;

	for (;;) {
		char method[128], *resp = NULL;
		snprintf(method, sizeof(method), "getUpdates?timeout=50&offset=%lld", offset);
		int st = api(method, NULL, 70, &resp);
		if (st != 200 || !resp) {
			free(resp);
			if (st == 401 || st == 404) {
				set_status("badtoken");
				sleep(60);
			} else {
				set_status("offline");
				sleep(backoff);
				backoff = backoff < 60 ? backoff * 2 : 60;
			}
			continue;
		}
		backoff = 2;
		pthread_mutex_lock(&lock);
		set_status(cfg.chat_id ? "ok" : "pair");
		pthread_mutex_unlock(&lock);

		jsmn_parser p;
		jsmn_init(&p);
		int n = jsmn_parse(&p, resp, strlen(resp), NULL, 0);
		jsmntok_t *t = n > 0 ? malloc(sizeof(jsmntok_t) * n) : NULL;
		if (t) {
			jsmn_init(&p);
			jsmn_parse(&p, resp, strlen(resp), t, n);
			int res = find(resp, t, 0, "result");
			if (res >= 0 && t[res].type == JSMN_ARRAY) {
				int i = res + 1;
				for (int k = 0; k < t[res].size; k++) {
					int iu = find(resp, t, i, "update_id");
					int im = find(resp, t, i, "message");
					if (iu >= 0 && tok_ll(resp, &t[iu]) >= offset)
						offset = tok_ll(resp, &t[iu]) + 1;
					if (im >= 0)
						handle_message(resp, t, im);
					i = skip(t, i);
				}
				save_offset(offset);
			}
			free(t);
		}
		free(resp);
	}
	return NULL;
}

/* ---------------- invio ---------------- */

static void send_pending(void)
{
	static long pos = -1;
	if (pos < 0) {
		FILE *pf = fopen(MSG_DIR "/outbox.pos", "r");
		pos = 0;
		if (pf) {
			if (fscanf(pf, "%ld", &pos) != 1)
				pos = 0;
			fclose(pf);
		}
	}
	FILE *f = fopen(MSG_OUTBOX, "r");
	if (!f)
		return;
	fseek(f, pos, SEEK_SET);
	char line[8400];
	while (fgets(line, sizeof(line), f)) {
		if (!strchr(line, '\n'))
			break; /* riga non ancora completa */
		line[strcspn(line, "\n")] = 0;
		char *tab = strchr(line, '\t');
		if (!tab)
			goto next;
		*tab = 0;
		char text[8192];
		msg_unescape(tab + 1, text, sizeof(text));

		pthread_mutex_lock(&lock);
		long long chat = cfg.chat_id;
		pthread_mutex_unlock(&lock);
		if (!chat)
			break; /* non ancora abbinato: riprova più tardi */
		int tries = 0;
		int is_photo = !strncmp(text, PHOTO_TAG, strlen(PHOTO_TAG));
		while ((is_photo ? send_photo(chat, text + strlen(PHOTO_TAG))
				 : send_text(chat, text)) != 200) {
			set_status("offline");
			sleep(tries < 5 ? 2 << tries : 60);
			tries++;
		}
		append_log(line, time(NULL), 'O', text);
next:
		pos = ftell(f);
		FILE *pf = fopen(MSG_DIR "/outbox.pos", "w");
		if (pf) {
			fprintf(pf, "%ld\n", pos);
			fclose(pf);
		}
	}
	fclose(f);
}

static void *sender(void *arg)
{
	(void)arg;
	int in = inotify_init1(IN_CLOEXEC);
	inotify_add_watch(in, MSG_DIR, IN_CLOSE_WRITE | IN_MODIFY | IN_MOVED_TO);
	for (;;) {
		send_pending();
		char buf[4096];
		/* aspetta modifiche, o al massimo un minuto (riprova invii in sospeso) */
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(in, &fds);
		struct timeval tv = { 60, 0 };
		if (select(in + 1, &fds, NULL, NULL, &tv) > 0 && read(in, buf, sizeof(buf)) < 0)
			sleep(1);
	}
	return NULL;
}

int main(void)
{
	mkdir("/data", 0755);
	mkdir(MSG_DIR, 0755);
	mkdir(MSG_RUN, 0755);

	unsigned r = 0;
	int ur = open("/dev/urandom", O_RDONLY);
	if (ur >= 0) {
		if (read(ur, &r, sizeof(r)) < 0)
			r = (unsigned)time(NULL);
		close(ur);
	}
	snprintf(pair_code, sizeof(pair_code), "%04u", r % 10000);

	load_conf();
	while (!cfg.token[0]) {
		set_status("notoken");
		sleep(10);
		load_conf();
	}
	status = "";
	set_status(cfg.chat_id ? "connecting" : "pair");
	if (!cfg.chat_id)
		fprintf(stderr, "msgd: codice di abbinamento %s\n", pair_code);

	pthread_t th;
	pthread_create(&th, NULL, sender, NULL);
	receiver(NULL);
	return 0;
}
