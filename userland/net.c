/* OSWiko - client HTTPS minimale con mbedTLS */
#define _GNU_SOURCE
#include "net.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#define MAX_RESPONSE (1 << 20)

static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;
static int inited;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;
static mbedtls_x509_crt ca;
static pthread_mutex_t rng_lock = PTHREAD_MUTEX_INITIALIZER;

/* Il generatore casuale è condiviso tra i thread: lo proteggiamo. */
static int rng(void *p, unsigned char *out, size_t len)
{
	pthread_mutex_lock(&rng_lock);
	int r = mbedtls_ctr_drbg_random(p, out, len);
	pthread_mutex_unlock(&rng_lock);
	return r;
}

static int net_init(const char *ca_file)
{
	pthread_mutex_lock(&init_lock);
	if (!inited) {
		psa_crypto_init();
		mbedtls_entropy_init(&entropy);
		mbedtls_ctr_drbg_init(&drbg);
		mbedtls_x509_crt_init(&ca);
		const char *pers = "oswiko";
		if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
					  (const unsigned char *)pers, strlen(pers)) != 0) {
			fprintf(stderr, "net: seed fallito\n");
		} else {
			int r = mbedtls_x509_crt_parse_file(&ca, ca_file);
			if (r < 0)
				fprintf(stderr, "net: certificati CA non caricati (%d)\n", r);
			inited = 1;
		}
	}
	pthread_mutex_unlock(&init_lock);
	return inited ? 0 : -1;
}

/* Decodifica il "chunked transfer encoding" sul posto. */
static size_t dechunk(char *b, size_t n)
{
	size_t in = 0, out = 0;
	while (in < n) {
		size_t len = strtoul(b + in, NULL, 16);
		char *eol = strstr(b + in, "\r\n");
		if (!eol || len == 0)
			break;
		in = eol - b + 2;
		if (in + len > n)
			len = n - in;
		memmove(b + out, b + in, len);
		out += len;
		in += len + 2;
	}
	b[out] = 0;
	return out;
}

int https_request(const struct https_target *t, const char *method,
		  const char *path, const char *body, int timeout_s,
		  char **body_out)
{
	mbedtls_net_context fd;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	char port[8], *buf = NULL, *req = NULL;
	size_t len = 0, cap = 0;
	int status = -1, r;

	*body_out = NULL;
	if (net_init(t->ca_file) < 0)
		return -1;

	mbedtls_net_init(&fd);
	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&conf);
	snprintf(port, sizeof(port), "%d", t->port);

	if ((r = mbedtls_net_connect(&fd, t->ip ? t->ip : t->host, port,
				     MBEDTLS_NET_PROTO_TCP)) != 0) {
		fprintf(stderr, "net: connessione a %s fallita (%d)\n", t->host, r);
		goto out;
	}
	mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
				    MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
	mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
	mbedtls_ssl_conf_ca_chain(&conf, &ca, NULL);
	mbedtls_ssl_conf_rng(&conf, rng, &drbg);
	mbedtls_ssl_conf_read_timeout(&conf, timeout_s * 1000);
	if (mbedtls_ssl_setup(&ssl, &conf) != 0 ||
	    mbedtls_ssl_set_hostname(&ssl, t->host) != 0)
		goto out;
	mbedtls_ssl_set_bio(&ssl, &fd, mbedtls_net_send, NULL, mbedtls_net_recv_timeout);

	while ((r = mbedtls_ssl_handshake(&ssl)) != 0) {
		if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
			char err[128];
			mbedtls_strerror(r, err, sizeof(err));
			fprintf(stderr, "net: TLS con %s fallito: %s\n", t->host, err);
			goto out;
		}
	}

	size_t blen = body ? strlen(body) : 0;
	size_t qlen = strlen(path) + strlen(t->host) + blen + 256;
	req = malloc(qlen);
	if (!req)
		goto out;
	int n = snprintf(req, qlen,
			 "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: OSWiko\r\n"
			 "Connection: close\r\nContent-Type: application/json\r\n"
			 "Content-Length: %zu\r\n\r\n%s",
			 method, path, t->host, blen, body ? body : "");
	for (int off = 0; off < n;) {
		r = mbedtls_ssl_write(&ssl, (unsigned char *)req + off, n - off);
		if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
			continue;
		if (r < 0)
			goto out;
		off += r;
	}

	for (;;) {
		if (cap - len < 4096) {
			cap = cap ? cap * 2 : 16384;
			if (cap > MAX_RESPONSE)
				break;
			char *nb = realloc(buf, cap + 1);
			if (!nb)
				goto out;
			buf = nb;
		}
		r = mbedtls_ssl_read(&ssl, (unsigned char *)buf + len, cap - len);
		if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE ||
		    r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
			continue;
		if (r <= 0) /* 0 o close_notify = fine risposta */
			break;
		len += r;
	}
	if (!buf)
		goto out;
	buf[len] = 0;

	char *hdr_end = strstr(buf, "\r\n\r\n");
	if (strncmp(buf, "HTTP/1.", 7) != 0 || !hdr_end)
		goto out;
	status = atoi(buf + 9);
	*hdr_end = 0;
	char *b = hdr_end + 4;
	size_t bl = len - (b - buf);
	for (char *h = strstr(buf, "\r\n"); h; h = strstr(h + 2, "\r\n"))
		if (!strncasecmp(h + 2, "Transfer-Encoding:", 18) && strcasestr(h, "chunked")) {
			bl = dechunk(b, bl);
			break;
		}
	*body_out = malloc(bl + 1);
	if (*body_out) {
		memcpy(*body_out, b, bl);
		(*body_out)[bl] = 0;
	}

out:
	mbedtls_ssl_close_notify(&ssl);
	mbedtls_net_free(&fd);
	mbedtls_ssl_free(&ssl);
	mbedtls_ssl_config_free(&conf);
	free(buf);
	free(req);
	return status;
}
