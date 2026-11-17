/* OSWiko - funzioni comuni a msgd e all'app Messaggi */
#include "msg.h"

void msg_escape(const char *in, char *out, int n)
{
	int k = 0;
	for (; *in && k < n - 3; in++) {
		if (*in == '\n') { out[k++] = '\\'; out[k++] = 'n'; }
		else if (*in == '\t') { out[k++] = '\\'; out[k++] = 't'; }
		else if (*in == '\\') { out[k++] = '\\'; out[k++] = '\\'; }
		else if (*in != '\r') out[k++] = *in;
	}
	out[k] = 0;
}

void msg_unescape(const char *in, char *out, int n)
{
	int k = 0;
	for (; *in && k < n - 1; in++) {
		if (*in == '\\' && in[1]) {
			in++;
			out[k++] = *in == 'n' ? '\n' : *in == 't' ? '\t' : *in;
		} else {
			out[k++] = *in;
		}
	}
	out[k] = 0;
}
