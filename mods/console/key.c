#define _GNU_SOURCE

#include "cn.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

extern int cn_fd;
extern int cn_on;

str cn_pend;
int cn_pendo;

/* Drop the bytes already turned into keys, keeping the buffer small. */
void cn_eat(size_t n)
{
	cn_pendo += (int)n;
	if ((size_t)cn_pendo >= cn_pend.n) {
		cn_pend.n = 0;
		cn_pendo = 0;
		if (cn_pend.p)
			cn_pend.p[0] = 0;
	}
}

/* Wait for the terminal to have something to say. */
int cn_wait(int ms)
{
	fd_set r;
	struct timeval tv;
	int k;

	FD_ZERO(&r);
	FD_SET(cn_fd, &r);
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	k = select(cn_fd + 1, &r, 0, 0, ms < 0 ? 0 : &tv);
	if (k > 0)
		return 1;
	if (k == 0)
		return 0;
	if (errno == EINTR) {
		lg(HIBR_LDBG, "screen: wait interrupted, probably a resize");
		return 0;
	}
	return -1;
}

/* Read whatever is waiting onto the pending buffer. */
int cn_rdfill(void)
{
	char b[512];
	ssize_t k;

	k = read(cn_fd, b, sizeof b);
	if (k > 0) {
		s_add(&cn_pend, b, (size_t)k);
		return 1;
	}
	if (k < 0 && (errno == EINTR || errno == EAGAIN))
		return 0;
	return -1;
}

/* Name the modifier bits xterm packs into a CSI parameter. */
void cn_mods(str *o, int m)
{
	int b = m > 0 ? m - 1 : 0;

	if (b & 4)
		s_cat(o, "ctrl-");
	if (b & 2)
		s_cat(o, "alt-");
	if (b & 1)
		s_cat(o, "shift-");
}

/* The name of a tilde-terminated CSI key, or null. */
const char *cn_tilde(int n)
{
	switch (n) {
	case 1:
	case 7:
		return "home";
	case 2:
		return "insert";
	case 3:
		return "delete";
	case 4:
	case 8:
		return "end";
	case 5:
		return "pageup";
	case 6:
		return "pagedown";
	case 11:
		return "f1";
	case 12:
		return "f2";
	case 13:
		return "f3";
	case 14:
		return "f4";
	case 15:
		return "f5";
	case 17:
		return "f6";
	case 18:
		return "f7";
	case 19:
		return "f8";
	case 20:
		return "f9";
	case 21:
		return "f10";
	case 23:
		return "f11";
	case 24:
		return "f12";
	}
	return 0;
}

/* The name of a letter-terminated CSI or SS3 key, or null. */
const char *cn_final(char c)
{
	switch (c) {
	case 'A':
		return "up";
	case 'B':
		return "down";
	case 'C':
		return "right";
	case 'D':
		return "left";
	case 'H':
		return "home";
	case 'F':
		return "end";
	case 'P':
		return "f1";
	case 'Q':
		return "f2";
	case 'R':
		return "f3";
	case 'S':
		return "f4";
	case 'E':
		return "begin";
	}
	return 0;
}

/* Name a control byte the way a key map would want to see it. */
void cn_ctrl(str *o, unsigned char c)
{
	if (c == 9) {
		s_cat(o, "tab");
	} else if (c == 13 || c == 10) {
		s_cat(o, "enter");
	} else if (c == 127 || c == 8) {
		s_cat(o, "backspace");
	} else if (c == 0) {
		s_cat(o, "ctrl-space");
	} else if (c < 27) {
		s_cat(o, "ctrl-");
		s_ch(o, (char)('a' + c - 1));
	} else {
		s_cat(o, "ctrl-");
		s_ch(o, (char)('a' + c - 1));
	}
}

/* Decode one mouse report in SGR form. */
int cn_mouse(const char *p, size_t n, str *o, size_t *used)
{
	int b = 0, x = 0, y = 0, i = 3, rel;

	if (n < 9)
		return 0;
	for (; i < (int)n && p[i] >= '0' && p[i] <= '9'; i++)
		b = b * 10 + (p[i] - '0');
	if (i >= (int)n || p[i] != ';')
		return i >= (int)n ? 0 : -1;
	for (i++; i < (int)n && p[i] >= '0' && p[i] <= '9'; i++)
		x = x * 10 + (p[i] - '0');
	if (i >= (int)n || p[i] != ';')
		return i >= (int)n ? 0 : -1;
	for (i++; i < (int)n && p[i] >= '0' && p[i] <= '9'; i++)
		y = y * 10 + (p[i] - '0');
	if (i >= (int)n)
		return 0;
	if (p[i] != 'M' && p[i] != 'm')
		return -1;
	rel = p[i] == 'm';
	s_cat(o, "mouse ");
	if (b & 64)
		s_cat(o, b & 1 ? "wheeldown" : "wheelup");
	else if (rel)
		s_cat(o, "release");
	else if ((b & 3) == 0)
		s_cat(o, "left");
	else if ((b & 3) == 1)
		s_cat(o, "middle");
	else if ((b & 3) == 2)
		s_cat(o, "right");
	else
		s_cat(o, "move");
	s_ch(o, ' ');
	s_num(o, (long)y);
	s_ch(o, ' ');
	s_num(o, (long)x);
	*used = (size_t)i + 1;
	return 1;
}

/* Collect a bracketed paste up to its terminator. */
int cn_paste(const char *p, size_t n, str *o, size_t *used)
{
	size_t i;

	for (i = 6; i + 5 < n + 1; i++)
		if (i + 6 <= n && !memcmp(p + i, "\033[201~", 6)) {
			s_cat(o, "paste ");
			s_add(o, p + 6, i - 6);
			*used = i + 6;
			return 1;
		}
	return 0;
}

/* Turn the front of the buffer into one key name; 0 means need more bytes. */
int cn_dec(const char *p, size_t n, str *o, size_t *used, int last)
{
	unsigned cp;
	int l, np = 0, par[4], i;
	const char *nm;

	if (!n)
		return 0;
	if ((unsigned char)p[0] != 27) {
		if ((unsigned char)p[0] < 32 || (unsigned char)p[0] == 127) {
			cn_ctrl(o, (unsigned char)p[0]);
			*used = 1;
			return 1;
		}
		l = u8dec(p, n, &cp);
		if ((unsigned char)p[0] >= 0xC0 && (size_t)l > n)
			return 0;
		s_add(o, p, (size_t)l);
		*used = (size_t)l;
		return 1;
	}
	if (n == 1)
		return last ? (s_cat(o, "escape"), *used = 1, 1) : 0;
	if (p[1] == '[' && n >= 3 && p[2] == '<')
		return cn_mouse(p, n, o, used);
	if (p[1] == '[' && n >= 6 && !memcmp(p + 2, "200~", 4))
		return cn_paste(p, n, o, used);
	if (p[1] == 'O' || p[1] == '[') {
		for (i = 0; i < 4; i++)
			par[i] = 0;
		i = 2;
		if (p[1] == '[' && i < (int)n && p[i] == '?')
			i++;
		while (i < (int)n) {
			if (p[i] >= '0' && p[i] <= '9') {
				if (np < 4)
					par[np] = par[np] * 10 + (p[i] - '0');
				i++;
				continue;
			}
			if (p[i] == ';') {
				if (++np > 3)
					np = 3;
				i++;
				continue;
			}
			break;
		}
		if (i >= (int)n)
			return 0;
		np++;
		if (p[i] == '~') {
			nm = cn_tilde(par[0]);
			if (!nm)
				return -1;
			cn_mods(o, np > 1 ? par[1] : 0);
			s_cat(o, nm);
			*used = (size_t)i + 1;
			return 1;
		}
		if (p[i] == 'Z') {
			s_cat(o, "shift-tab");
			*used = (size_t)i + 1;
			return 1;
		}
		nm = cn_final(p[i]);
		if (!nm)
			return -1;
		cn_mods(o, np > 1 ? par[1] : 0);
		s_cat(o, nm);
		*used = (size_t)i + 1;
		return 1;
	}
	s_cat(o, "alt-");
	if ((unsigned char)p[1] < 32 || (unsigned char)p[1] == 127) {
		cn_ctrl(o, (unsigned char)p[1]);
		*used = 2;
		return 1;
	}
	l = u8dec(p + 1, n - 1, &cp);
	if ((unsigned char)p[1] >= 0xC0 && (size_t)l > n - 1)
		return 0;
	s_add(o, p + 1, (size_t)l);
	*used = 1 + (size_t)l;
	return 1;
}

/* Read one key, waiting at most ms milliseconds; 0 means nothing arrived. */
int cn_key(int ms, str *out)
{
	size_t used = 0;
	int r, waited = 0;

	if (!cn_on) {
		lg(HIBR_LERR, "screen: not open");
		return -1;
	}
	for (;;) {
		if (cn_pend.n > (size_t)cn_pendo) {
			r = cn_dec(cn_pend.p + cn_pendo,
				   cn_pend.n - (size_t)cn_pendo, out, &used,
				   waited);
			if (r == 1) {
				cn_eat(used);
				return 1;
			}
			if (r < 0) {
				lg(HIBR_LDBG, "screen: unknown sequence, "
					      "skipping a byte");
				cn_eat(1);
				out->n = 0;
				if (out->p)
					out->p[0] = 0;
				continue;
			}
			r = cn_wait(waited ? 0 : 50);
			if (r <= 0) {
				if (waited)
					return 0;
				waited = 1;
				continue;
			}
			if (cn_rdfill() < 0)
				return -1;
			continue;
		}
		r = cn_wait(ms);
		if (r == 0)
			return 0;
		if (r < 0)
			return -1;
		if (cn_rdfill() < 0)
			return -1;
		waited = 0;
	}
}
