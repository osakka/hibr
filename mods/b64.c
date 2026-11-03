#include "b64.h"

/* Append n bytes as base64, padded with = to a multiple of four. */
void b64_enc(str *o, const unsigned char *p, size_t n)
{
	static const char al[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	unsigned v;
	size_t i;

	for (i = 0; i + 2 < n; i += 3) {
		v = (unsigned)p[i] << 16 | (unsigned)p[i + 1] << 8 | p[i + 2];
		s_ch(o, al[v >> 18 & 63]);
		s_ch(o, al[v >> 12 & 63]);
		s_ch(o, al[v >> 6 & 63]);
		s_ch(o, al[v & 63]);
	}
	if (n - i == 1) {
		v = (unsigned)p[i] << 16;
		s_ch(o, al[v >> 18 & 63]);
		s_ch(o, al[v >> 12 & 63]);
		s_cat(o, "==");
	} else if (n - i == 2) {
		v = (unsigned)p[i] << 16 | (unsigned)p[i + 1] << 8;
		s_ch(o, al[v >> 18 & 63]);
		s_ch(o, al[v >> 12 & 63]);
		s_ch(o, al[v >> 6 & 63]);
		s_ch(o, '=');
	}
}

/* Append what n characters of base64 decode to; see b64.h for what it
   takes and what it answers. */
long b64_dec(str *o, const char *p, size_t n, int strict)
{
	unsigned v = 0;
	int bits = 0, d;
	size_t i;
	long out = 0;
	char c;

	for (i = 0; i < n; i++) {
		c = p[i];
		if (c >= 'A' && c <= 'Z')
			d = c - 'A';
		else if (c >= 'a' && c <= 'z')
			d = c - 'a' + 26;
		else if (c >= '0' && c <= '9')
			d = c - '0' + 52;
		else if (c == '+' || c == '-')
			d = 62;
		else if (c == '/' || c == '_')
			d = 63;
		else if (c == '=')
			break;
		else if (c == '\n' || c == '\r' || !strict)
			continue;
		else
			return -1;
		v = v << 6 | (unsigned)d;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			s_ch(o, (int)(v >> bits & 255));
			out++;
		}
	}
	return out;
}
