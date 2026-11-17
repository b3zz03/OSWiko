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

/* Esegue una richiesta. *body_out va liberato con free().
 * Ritorna il codice HTTP (200...) oppure -1 in caso di errore di rete. */
int https_request(const struct https_target *t, const char *method,
		  const char *path, const char *body, int timeout_s,
		  char **body_out);

#endif
