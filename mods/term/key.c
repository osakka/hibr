#define _GNU_SOURCE

#include "tm.h"
#include <string.h>

struct tm_kn { const char *nm; const char *seq; };

/* The names the console decodes, turned back into what a program expects.

   This is the console's key table read backwards, and it has to be: the
   window manager hands an app a name like "pageup", and the program inside
   the window is waiting for the bytes a terminal would have sent. Anything
   the console learns to decode has to appear here too, or that key reaches
   the terminal window and stops. */
static const struct tm_kn tm_keys[] = {
	{ "up", "\033[A" }, { "down", "\033[B" },
	{ "right", "\033[C" }, { "left", "\033[D" },
	{ "home", "\033[H" }, { "end", "\033[F" },
	{ "pageup", "\033[5~" }, { "pagedown", "\033[6~" },
	{ "insert", "\033[2~" }, { "delete", "\033[3~" },
	{ "enter", "\r" }, { "tab", "\t" }, { "escape", "\033" },
	{ "backspace", "\177" }, { "space", " " },
	{ "shift-tab", "\033[Z" },
	{ "shift-up", "\033[1;2A" }, { "shift-down", "\033[1;2B" },
	{ "shift-right", "\033[1;2C" }, { "shift-left", "\033[1;2D" },
	{ "ctrl-up", "\033[1;5A" }, { "ctrl-down", "\033[1;5B" },
	{ "ctrl-right", "\033[1;5C" }, { "ctrl-left", "\033[1;5D" },
	{ "f1", "\033OP" }, { "f2", "\033OQ" }, { "f3", "\033OR" },
	{ "f4", "\033OS" }, { "f5", "\033[15~" }, { "f6", "\033[17~" },
	{ "f7", "\033[18~" }, { "f8", "\033[19~" }, { "f9", "\033[20~" },
	{ "f10", "\033[21~" }, { "f11", "\033[23~" }, { "f12", "\033[24~" },
	{ 0, 0 }
};

/* Turn a decoded key name into the bytes to send.  Returns 0 for a name
   that means nothing to a program, so the caller can leave it alone. */
int tm_keybytes(const char *name, str *out)
{
	const struct tm_kn *k;
	size_t n;

	if (!name || !*name)
		return 0;
	for (k = tm_keys; k->nm; k++)
		if (!strcmp(name, k->nm)) {
			s_cat(out, k->seq);
			return 1;
		}
	if (!strncmp(name, "ctrl-", 5) && name[5] && !name[6]) {
		int c = name[5];
		if (c >= 'a' && c <= 'z')
			s_ch(out, c - 'a' + 1);
		else if (c == ' ' || c == '@')
			s_ch(out, 0);
		else if (c >= '[' && c <= '_')
			s_ch(out, c - '@');
		else
			return 0;
		return 1;
	}
	if (!strncmp(name, "alt-", 4) && name[4]) {
		s_ch(out, 033);
		s_cat(out, name + 4);
		return 1;
	}
	if (!strncmp(name, "paste ", 6)) {
		s_cat(out, name + 6);
		return 1;
	}
	if (!strncmp(name, "mouse ", 6))
		return 0;
	n = strlen(name);
	if (u8len((unsigned char)name[0]) == (int)n) {
		s_cat(out, name);
		return 1;
	}
	return 0;
}

/* What the program inside has asked to hear from the mouse. */
const char *tm_mname(tm_t *t)
{
	switch (t->mmode) {
	case 9:
	case 1000:
		return "click";
	case 1002:
		return "drag";
	case 1003:
		return "motion";
	}
	return "off";
}

/* Encode a mouse event for the program, if it asked for this kind.

   This is the console's cn_mouse read backwards: a name such as `press left`
   or `wheelup` at a cell counted from zero becomes the report a terminal
   would send.  SGR form when the program turned on 1006, since it has no
   limit on the position and says which button was released; the old form
   otherwise, which cannot name a column past 223 and so drops the event
   rather than send a wrong one.  Returns 0 for an event the program did not
   ask for, so the caller can use it for something else. */
int tm_mouse(tm_t *t, const char *act, const char *btn, int r, int c,
	     str *out)
{
	int b, rel = 0;

	if (!t->mmode)
		return 0;
	if (!strcmp(btn, "middle"))
		b = 1;
	else if (!strcmp(btn, "right"))
		b = 2;
	else
		b = 0;
	if (!strcmp(act, "wheelup") || !strcmp(act, "wheeldown")) {
		if (t->mmode == 9)
			return 0;
		b = act[5] == 'u' ? 64 : 65;
	} else if (!strcmp(act, "release")) {
		if (t->mmode == 9)
			return 0;
		rel = 1;
	} else if (!strcmp(act, "drag")) {
		if (t->mmode != 1002 && t->mmode != 1003)
			return 0;
		b += 32;
	} else if (strcmp(act, "press")) {
		return 0;
	}
	if (r < 0)
		r = 0;
	if (c < 0)
		c = 0;
	if (r >= t->rows)
		r = t->rows - 1;
	if (c >= t->cols)
		c = t->cols - 1;
	if (t->msgr) {
		s_cat(out, "\033[<");
		s_num(out, (long)b);
		s_ch(out, ';');
		s_num(out, (long)c + 1);
		s_ch(out, ';');
		s_num(out, (long)r + 1);
		s_ch(out, rel ? 'm' : 'M');
		return 1;
	}
	if (c + 1 + 32 > 255 || r + 1 + 32 > 255)
		return 1;
	s_cat(out, "\033[M");
	s_ch(out, (char)(32 + (rel ? 3 : b)));
	s_ch(out, (char)(32 + c + 1));
	s_ch(out, (char)(32 + r + 1));
	return 1;
}
