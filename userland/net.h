/* OSWiko - client HTTPS minimale (mbedTLS) */
#ifndef OSWIKO_NET_H
#define OSWIKO_NET_H

#include <stddef.h>

struct https_target {
	const char *host;     /* nome per SNI e verifica del certificato */
	const char *ip;       /* opzionale: indirizzo a cui connettersi */
	int port;
	const char *ca_file;  /* certificati delle autorità (PEM) */
};

/* Esegue una richiesta. body può contenere dati binari (blen byte);
 * ctype è il Content-Type (NULL = application/json). *out va liberato con
 * free() ed è sempre terminato da uno 0 (comodo per il JSON).
 * Ritorna il codice HTTP (200...) oppure -1 in caso di errore di rete. */
int https_request(const struct https_target *t, const char *method,
		  const char *path, const void *body, size_t blen,
		  const char *ctype, int timeout_s, char **out, size_t *outlen);

#endif
