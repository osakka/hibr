#define _GNU_SOURCE

#include "hibr.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/*
 * dbus -- the session bus, for the desktop: the tray (Gitea #505). hibr is
 * the session's StatusNotifierWatcher when nobody else is, and its host
 * always: programs register their tray items, and the desktop reads each
 * one's icon, title and menu (com.canonical.dbusmenu) and sends clicks
 * back. libdbus-1 is dlopen'd and its few types declared here, as libssl's
 * and libpam's are elsewhere; the connection is a private one, never the
 * shared one, so a bus that goes away is an event and not the end of the
 * process, and SIGPIPE is left as it was.
 *
 * Nothing here waits on the bus. Every exchange with the daemon -- Hello,
 * RequestName, AddMatch, every property read and every menu -- is a
 * message sent and its reply taken up later, matched by serial, by `dbus
 * pump`, which the desktop runs when the bus's descriptor wakes it (the
 * Quran's media open held the desktop a second at a time; a bus call
 * would be the same).
 */

/* The shell's own, not in the module ABI's header; media declares it the same way. */
void v_del(sh *s, const char *k);

typedef unsigned int bu_bool;
typedef struct bu_conn bu_conn;
typedef struct bu_msg bu_msg;

/* libdbus's DBusError: two pointers, five one-bit fields and a pointer,
   32 bytes on LP64 -- its header fixes the layout; made larger here. */
typedef struct {
	const char *name;
	const char *message;
	unsigned int bits;
	void *pad[8];
} bu_err;

/* libdbus's DBusMessageIter: two pointers, nine ints and two pointers, 72
   bytes on LP64, allocated by the caller -- its header fixes the layout;
   made larger here. */
typedef union {
	void *p[24];
	long double ld;
} bu_it;

#define BU_CALL 1
#define BU_RET 2
#define BU_ERROR 3
#define BU_SIGNAL 4
#define BU_DATA 0
#define BU_HANDLED 0
#define BU_NOTYET 1

#ifndef BU_MAXITEMS
#define BU_MAXITEMS 64
#endif
#ifndef BU_MAXROWS
#define BU_MAXROWS 400
#endif
#ifndef BU_MAXDEPTH
#define BU_MAXDEPTH 8
#endif
#ifndef BU_MAXTEXT
#define BU_MAXTEXT 512
#endif
#ifndef BU_MAXPX
#define BU_MAXPX 1024
#endif
#ifndef BU_WAITSEC
#define BU_WAITSEC 10
#endif

typedef struct {
	void *h;
	void (*err_init)(bu_err *);
	void (*err_free)(bu_err *);
	void (*sigpipe)(bu_bool);
	bu_conn *(*open)(const char *, bu_err *);
	void (*exitdis)(bu_conn *, bu_bool);
	bu_bool (*getfd)(bu_conn *, int *);
	bu_bool (*rw)(bu_conn *, int);
	int (*dispatch)(bu_conn *);
	int (*dstatus)(bu_conn *);
	bu_bool (*filter)(bu_conn *, int (*)(bu_conn *, bu_msg *, void *), void *, void (*)(void *));
	bu_bool (*send)(bu_conn *, bu_msg *, unsigned int *);
	void (*close)(bu_conn *);
	void (*unref)(bu_conn *);
	bu_bool (*live)(bu_conn *);
	bu_bool (*tosend)(bu_conn *);
	bu_bool (*setname)(bu_conn *, const char *);
	bu_msg *(*mcall)(const char *, const char *, const char *, const char *);
	bu_msg *(*msignal)(const char *, const char *, const char *);
	bu_msg *(*mreturn)(bu_msg *);
	bu_msg *(*merror)(bu_msg *, const char *, const char *);
	void (*munref)(bu_msg *);
	int (*mtype)(bu_msg *);
	const char *(*msender)(bu_msg *);
	const char *(*mpath)(bu_msg *);
	const char *(*miface)(bu_msg *);
	const char *(*mmember)(bu_msg *);
	unsigned int (*mrserial)(bu_msg *);
	void (*mnoreply)(bu_msg *, bu_bool);
	bu_bool (*iinit)(bu_msg *, bu_it *);
	int (*itype)(bu_it *);
	void (*iget)(bu_it *, void *);
	bu_bool (*inext)(bu_it *);
	void (*irecurse)(bu_it *, bu_it *);
	int (*ietype)(bu_it *);
	void (*ifixed)(bu_it *, void *, int *);
	void (*iappinit)(bu_msg *, bu_it *);
	bu_bool (*iappend)(bu_it *, int, const void *);
	bu_bool (*iopen)(bu_it *, int, const char *, bu_it *);
	bu_bool (*iclose)(bu_it *, bu_it *);
} bu_lib;

static bu_lib bu;

typedef struct {
	int id, depth, enabled, toggle, state, sub, sep;
	char *label;
} bu_row;

typedef struct {
	int n, live, fetching, stale, known, mfetching, mstale, mknown, mrev;
	char *name, *path, *owner, *key;
	char *sid, *title, *status, *category, *icon, *iconpath, *attention, *tip, *menu;
	int ismenu, pw, ph;
	unsigned char *px;
	bu_row *rows;
	int nrows;
} bu_item;

enum { K_HELLO = 1, K_WATCH, K_HOST, K_IGNORE, K_GETALL, K_OWNER, K_LAYOUT, K_WITEMS, K_ACTIVATE };

typedef struct {
	unsigned int serial;
	int kind, item;
	time_t at;
} bu_wait;

static bu_conn *bu_c;
static int bu_fd = -1;
static int bu_role;
static int bu_next;
static char *bu_me;
static vec bu_items;
static vec bu_evs;
static vec bu_waits;
static char *bu_hostname;

#define BU_BUS "org.freedesktop.DBus"
#define BU_BUSPATH "/org/freedesktop/DBus"
#define BU_WNAME "org.kde.StatusNotifierWatcher"
#define BU_WPATH "/StatusNotifierWatcher"
#define BU_IFACE "org.kde.StatusNotifierItem"
#define BU_MENU "com.canonical.dbusmenu"
#define BU_PROPS "org.freedesktop.DBus.Properties"

/* Load libdbus-1 and every function used; 0 when it is not there. */
int bu_load(void)
{
	static const char *names[] = { "libdbus-1.so.3", "libdbus-1.3.dylib", "libdbus-1.so", 0 };
	int i, bad = 0;
	void *h = 0;

	if (bu.h)
		return 1;
	for (i = 0; names[i] && !h; i++)
		h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
	if (!h) {
		lg(HIBR_LDBG, "dbus: no libdbus-1");
		return 0;
	}
#define S(f, n) do { *(void **)&bu.f = dlsym(h, n); if (!bu.f) { lg(HIBR_LERR, "dbus: libdbus-1 lacks %s", n); bad++; } } while (0)
	S(err_init, "dbus_error_init");
	S(err_free, "dbus_error_free");
	S(sigpipe, "dbus_connection_set_change_sigpipe");
	S(open, "dbus_connection_open_private");
	S(exitdis, "dbus_connection_set_exit_on_disconnect");
	S(getfd, "dbus_connection_get_unix_fd");
	S(rw, "dbus_connection_read_write");
	S(dispatch, "dbus_connection_dispatch");
	S(dstatus, "dbus_connection_get_dispatch_status");
	S(filter, "dbus_connection_add_filter");
	S(send, "dbus_connection_send");
	S(close, "dbus_connection_close");
	S(unref, "dbus_connection_unref");
	S(live, "dbus_connection_get_is_connected");
	S(tosend, "dbus_connection_has_messages_to_send");
	S(setname, "dbus_bus_set_unique_name");
	S(mcall, "dbus_message_new_method_call");
	S(msignal, "dbus_message_new_signal");
	S(mreturn, "dbus_message_new_method_return");
	S(merror, "dbus_message_new_error");
	S(munref, "dbus_message_unref");
	S(mtype, "dbus_message_get_type");
	S(msender, "dbus_message_get_sender");
	S(mpath, "dbus_message_get_path");
	S(miface, "dbus_message_get_interface");
	S(mmember, "dbus_message_get_member");
	S(mrserial, "dbus_message_get_reply_serial");
	S(mnoreply, "dbus_message_set_no_reply");
	S(iinit, "dbus_message_iter_init");
	S(itype, "dbus_message_iter_get_arg_type");
	S(iget, "dbus_message_iter_get_basic");
	S(inext, "dbus_message_iter_next");
	S(irecurse, "dbus_message_iter_recurse");
	S(ietype, "dbus_message_iter_get_element_type");
	S(ifixed, "dbus_message_iter_get_fixed_array");
	S(iappinit, "dbus_message_iter_init_append");
	S(iappend, "dbus_message_iter_append_basic");
	S(iopen, "dbus_message_iter_open_container");
	S(iclose, "dbus_message_iter_close_container");
#undef S
	if (bad) {
		dlclose(h);
		return 0;
	}
	bu.h = h;
	return 1;
}

/* A copy of text from the bus, cut to BU_MAXTEXT, its control bytes gone. */
char *bu_clean(const char *t)
{
	str o;
	size_t i;

	s_init(&o);
	s_cat(&o, "");
	for (i = 0; t && t[i] && o.n < BU_MAXTEXT; i++) {
		unsigned char c = (unsigned char)t[i];

		if (c < 0x20 || c == 0x7f)
			continue;
		s_ch(&o, (char)c);
	}
	return o.p;
}

/* Say an event for the next pump. */
void bu_ev(const char *what, int n)
{
	str o;

	s_init(&o);
	s_cat(&o, what);
	if (n >= 0) {
		s_ch(&o, ' ');
		s_num(&o, n);
	}
	v_add(&bu_evs, o.p);
}

/* Send m, remembering its serial against what its reply is for. */
void bu_sendw(bu_msg *m, int kind, int item)
{
	unsigned int serial = 0;
	bu_wait *w;

	if (!m)
		return;
	if (bu.send(bu_c, m, &serial) && kind != K_IGNORE) {
		w = xm(sizeof *w);
		memset(w, 0, sizeof *w);
		w->serial = serial;
		w->kind = kind;
		w->item = item;
		w->at = time(0);
		v_add(&bu_waits, w);
	}
	bu.munref(m);
}

/* Append one string to a message. */
void bu_putstr(bu_it *it, const char *v)
{
	bu.iappend(it, 's', &v);
}

/* A method call with string arguments, sent and its reply waited for. */
void bu_callw(const char *dest, const char *path, const char *iface, const char *member,
	      int kind, int item, const char *a1, const char *a2)
{
	bu_msg *m = bu.mcall(dest, path, iface, member);
	bu_it it;

	if (!m)
		return;
	bu.iappinit(m, &it);
	if (a1)
		bu_putstr(&it, a1);
	if (a2)
		bu_putstr(&it, a2);
	bu_sendw(m, kind, item);
}

/* The item whose number is n, or 0. */
bu_item *bu_item_n(int n)
{
	size_t i;

	for (i = 0; i < bu_items.n; i++)
		if (((bu_item *)bu_items.p[i])->n == n)
			return bu_items.p[i];
	return 0;
}

/* The item registered as key, or 0. */
bu_item *bu_item_key(const char *key)
{
	size_t i;

	for (i = 0; i < bu_items.n; i++)
		if (!strcmp(((bu_item *)bu_items.p[i])->key, key))
			return bu_items.p[i];
	return 0;
}

/* Free a menu's rows. */
void bu_rowsfree(bu_item *t)
{
	int i;

	for (i = 0; i < t->nrows; i++)
		free(t->rows[i].label);
	free(t->rows);
	t->rows = 0;
	t->nrows = 0;
}

/* Free one item's fields. */
void bu_itemfree(bu_item *t)
{
	free(t->name);
	free(t->path);
	free(t->owner);
	free(t->key);
	free(t->sid);
	free(t->title);
	free(t->status);
	free(t->category);
	free(t->icon);
	free(t->iconpath);
	free(t->attention);
	free(t->tip);
	free(t->menu);
	free(t->px);
	bu_rowsfree(t);
	free(t);
}

/* Ask an item for everything it says about itself. */
void bu_fetch(bu_item *t)
{
	if (t->fetching) {
		t->stale = 1;
		return;
	}
	t->fetching = 1;
	t->stale = 0;
	bu_callw(t->name, t->path, BU_PROPS, "GetAll", K_GETALL, t->n, BU_IFACE, 0);
}

/* Take an item registered by sender as service: a bus name, an object
   path (the sender is then the name), or a name followed by a path. */
bu_item *bu_additem(const char *sender, const char *service)
{
	str key;
	char *name, *path, *sl;
	bu_item *t;

	if (!service || !*service)
		return 0;
	if (service[0] == '/') {
		name = xs(sender ? sender : "");
		path = xs(service);
	} else if ((sl = strchr(service, '/'))) {
		name = xs(service);
		name[sl - service] = 0;
		path = xs(sl);
	} else {
		name = xs(service);
		path = xs("/StatusNotifierItem");
	}
	s_init(&key);
	s_cat(&key, name);
	s_cat(&key, path);
	if ((t = bu_item_key(key.p))) {
		free(name);
		free(path);
		s_free(&key);
		return t;
	}
	if (bu_items.n >= BU_MAXITEMS || !*name) {
		free(name);
		free(path);
		s_free(&key);
		return 0;
	}
	t = xm(sizeof *t);
	memset(t, 0, sizeof *t);
	t->n = ++bu_next;
	t->name = name;
	t->path = path;
	t->key = key.p;
	t->live = 1;
	v_add(&bu_items, t);
	if (name[0] == ':')
		t->owner = xs(name);
	else
		bu_callw(BU_BUS, BU_BUSPATH, BU_BUS, "GetNameOwner", K_OWNER, t->n, name, 0);
	bu_fetch(t);
	lg(HIBR_LDBG, "dbus: tray item %d is %s", t->n, t->key);
	return t;
}

/* Take an item away: said once, and the watcher's own signal sent. */
void bu_dropitem(bu_item *t)
{
	size_t i;
	bu_msg *m;
	bu_it it;

	for (i = 0; i < bu_items.n; i++)
		if (bu_items.p[i] == t)
			break;
	if (i == bu_items.n)
		return;
	memmove(bu_items.p + i, bu_items.p + i + 1, (bu_items.n - i - 1) * sizeof *bu_items.p);
	bu_items.n--;
	if (t->known)
		bu_ev("gone", t->n);
	if (bu_role == 2 && (m = bu.msignal(BU_WPATH, BU_WNAME, "StatusNotifierItemUnregistered"))) {
		bu.iappinit(m, &it);
		bu_putstr(&it, t->key);
		bu_sendw(m, K_IGNORE, 0);
	}
	bu_itemfree(t);
}

/* A string, an object path or a signature at it, or 0. */
const char *bu_gets(bu_it *it)
{
	const char *v = 0;
	int ty = bu.itype(it);

	if (ty == 's' || ty == 'o' || ty == 'g')
		bu.iget(it, &v);
	return v;
}

/* A number of any width at it, or d. */
long bu_getn(bu_it *it, long d)
{
	int ty = bu.itype(it);
	union {
		unsigned char y;
		int i;
		unsigned u;
		long long x;
		unsigned long long t;
		short n;
		unsigned short q;
	} v;

	memset(&v, 0, sizeof v);
	switch (ty) {
	case 'y': bu.iget(it, &v.y); return v.y;
	case 'b': case 'u': bu.iget(it, &v.u); return (long)v.u;
	case 'i': bu.iget(it, &v.i); return v.i;
	case 'n': bu.iget(it, &v.n); return v.n;
	case 'q': bu.iget(it, &v.q); return v.q;
	case 'x': bu.iget(it, &v.x); return (long)v.x;
	case 't': bu.iget(it, &v.t); return (long)v.t;
	}
	return d;
}

/* The best of an a(iiay) of pictures for a tray -- the smallest at least
   32 pixels wide, else the largest -- into t, bounded and checked. */
void bu_pixmaps(bu_item *t, bu_it *arr)
{
	bu_it st, f, by;
	int w, h, n, bw = 0, bh = 0;
	const unsigned char *p, *best = 0;

	if (bu.itype(arr) != 'a')
		return;
	bu.irecurse(arr, &st);
	while (bu.itype(&st) == 'r') {
		bu.irecurse(&st, &f);
		w = (int)bu_getn(&f, 0);
		bu.inext(&f);
		h = (int)bu_getn(&f, 0);
		bu.inext(&f);
		p = 0;
		n = 0;
		if (bu.itype(&f) == 'a' && bu.ietype(&f) == 'y') {
			bu.irecurse(&f, &by);
			bu.ifixed(&by, &p, &n);
		}
		if (w > 0 && h > 0 && w <= BU_MAXPX && h <= BU_MAXPX && p &&
		    (long)n == (long)w * h * 4) {
			int better = !best || (bw < 32 ? w > bw : (w >= 32 && w < bw));

			if (better) {
				best = p;
				bw = w;
				bh = h;
			}
		}
		bu.inext(&st);
	}
	if (!best)
		return;
	free(t->px);
	t->px = xm((size_t)bw * bh * 4);
	memcpy(t->px, best, (size_t)bw * bh * 4);
	t->pw = bw;
	t->ph = bh;
}

/* Replace a field with a cleaned copy of v. */
void bu_set(char **f, const char *v)
{
	free(*f);
	*f = bu_clean(v);
}

/* An item's properties, from GetAll's a{sv}. */
void bu_props(bu_item *t, bu_msg *m)
{
	bu_it it, arr, de, var, tt;
	const char *k, *v;

	if (!bu.iinit(m, &it) || bu.itype(&it) != 'a')
		return;
	bu.irecurse(&it, &arr);
	while (bu.itype(&arr) == 'e') {
		bu.irecurse(&arr, &de);
		k = bu_gets(&de);
		bu.inext(&de);
		if (k && bu.itype(&de) == 'v') {
			bu.irecurse(&de, &var);
			v = bu_gets(&var);
			if (!strcmp(k, "Id") && v)
				bu_set(&t->sid, v);
			else if (!strcmp(k, "Title") && v)
				bu_set(&t->title, v);
			else if (!strcmp(k, "Status") && v)
				bu_set(&t->status, v);
			else if (!strcmp(k, "Category") && v)
				bu_set(&t->category, v);
			else if (!strcmp(k, "IconName") && v)
				bu_set(&t->icon, v);
			else if (!strcmp(k, "IconThemePath") && v)
				bu_set(&t->iconpath, v);
			else if (!strcmp(k, "AttentionIconName") && v)
				bu_set(&t->attention, v);
			else if (!strcmp(k, "Menu") && v)
				bu_set(&t->menu, v);
			else if (!strcmp(k, "ItemIsMenu"))
				t->ismenu = bu_getn(&var, 0) != 0;
			else if (!strcmp(k, "IconPixmap"))
				bu_pixmaps(t, &var);
			else if (!strcmp(k, "ToolTip") && bu.itype(&var) == 'r') {
				bu.irecurse(&var, &tt);
				bu.inext(&tt);
				bu.inext(&tt);
				if ((v = bu_gets(&tt)))
					bu_set(&t->tip, v);
			}
		}
		bu.inext(&arr);
	}
}

/* A dbusmenu label as shown: one underscore marks a mnemonic and goes,
   two are one. */
char *bu_label(const char *t)
{
	str o;
	size_t i;

	s_init(&o);
	s_cat(&o, "");
	for (i = 0; t && t[i] && o.n < BU_MAXTEXT; i++) {
		unsigned char c = (unsigned char)t[i];

		if (c == '_') {
			if (t[i + 1] == '_') {
				s_ch(&o, '_');
				i++;
			}
			continue;
		}
		if (c < 0x20 || c == 0x7f)
			continue;
		s_ch(&o, (char)c);
	}
	return o.p;
}

/* One node of a dbusmenu layout, (ia{sv}av), into rows: its own when it
   is shown and is not the root, then its children one deeper. */
void bu_node(bu_item *t, bu_it *node, int depth, int root)
{
	bu_it f, arr, de, var, kids, kv;
	bu_row r;
	const char *k, *v;
	int visible = 1;

	if (depth > BU_MAXDEPTH || t->nrows >= BU_MAXROWS || bu.itype(node) != 'r')
		return;
	memset(&r, 0, sizeof r);
	r.enabled = 1;
	r.depth = depth;
	bu.irecurse(node, &f);
	r.id = (int)bu_getn(&f, 0);
	bu.inext(&f);
	if (bu.itype(&f) == 'a') {
		bu.irecurse(&f, &arr);
		while (bu.itype(&arr) == 'e') {
			bu.irecurse(&arr, &de);
			k = bu_gets(&de);
			bu.inext(&de);
			if (k && bu.itype(&de) == 'v') {
				bu.irecurse(&de, &var);
				v = bu_gets(&var);
				if (!strcmp(k, "label") && v) {
					free(r.label);
					r.label = bu_label(v);
				} else if (!strcmp(k, "enabled"))
					r.enabled = bu_getn(&var, 1) != 0;
				else if (!strcmp(k, "visible"))
					visible = bu_getn(&var, 1) != 0;
				else if (!strcmp(k, "type") && v)
					r.sep = !strcmp(v, "separator");
				else if (!strcmp(k, "toggle-type") && v)
					r.toggle = !strcmp(v, "checkmark") ? 1 : !strcmp(v, "radio") ? 2 : 0;
				else if (!strcmp(k, "toggle-state"))
					r.state = (int)bu_getn(&var, 0);
				else if (!strcmp(k, "children-display") && v)
					r.sub = !strcmp(v, "submenu");
			}
			bu.inext(&arr);
		}
	}
	bu.inext(&f);
	if (!visible) {
		free(r.label);
		return;
	}
	if (!root) {
		if (!r.label)
			r.label = xs("");
		t->rows = xr(t->rows, (size_t)(t->nrows + 1) * sizeof *t->rows);
		t->rows[t->nrows++] = r;
	} else {
		free(r.label);
	}
	if (bu.itype(&f) == 'a') {
		bu.irecurse(&f, &kids);
		while (bu.itype(&kids) == 'v') {
			bu.irecurse(&kids, &kv);
			bu_node(t, &kv, root ? depth : depth + 1, 0);
			bu.inext(&kids);
		}
	}
}

/* A GetLayout reply, (u (ia{sv}av)), into the item's rows. */
void bu_layout(bu_item *t, bu_msg *m)
{
	bu_it it;
	int i, j;

	bu_rowsfree(t);
	if (!bu.iinit(m, &it))
		return;
	t->mrev = (int)bu_getn(&it, 0);
	bu.inext(&it);
	bu_node(t, &it, 0, 1);
	for (i = 0; i < t->nrows; i++)
		for (j = i + 1; j < t->nrows && t->rows[j].depth > t->rows[i].depth; j++)
			t->rows[i].sub = 1;
}

/* Every item the watcher already knows, from its RegisteredStatusNotifierItems. */
void bu_witems(bu_msg *m)
{
	bu_it it, var, arr;
	const char *v;

	if (!bu.iinit(m, &it) || bu.itype(&it) != 'v')
		return;
	bu.irecurse(&it, &var);
	if (bu.itype(&var) != 'a')
		return;
	bu.irecurse(&var, &arr);
	while ((v = bu_gets(&arr))) {
		bu_additem(0, v);
		bu.inext(&arr);
	}
}

/* Ask the daemon for the signals the tray needs. */
void bu_matches(void)
{
	static const char *rules[] = {
		"type='signal',sender='" BU_BUS "',member='NameOwnerChanged'",
		"type='signal',interface='" BU_IFACE "'",
		"type='signal',interface='" BU_MENU "'",
		"type='signal',interface='" BU_WNAME "'",
		0
	};
	int i;

	for (i = 0; rules[i]; i++)
		bu_callw(BU_BUS, BU_BUSPATH, BU_BUS, "AddMatch", K_IGNORE, 0, rules[i], 0);
}

/* Ask for a name, not queued behind whoever has it. */
void bu_request(const char *name, int kind)
{
	bu_msg *m = bu.mcall(BU_BUS, BU_BUSPATH, BU_BUS, "RequestName");
	bu_it it;
	unsigned int fl = 4;

	if (!m)
		return;
	bu.iappinit(m, &it);
	bu_putstr(&it, name);
	bu.iappend(&it, 'u', &fl);
	bu_sendw(m, kind, 0);
}

/* As host only: tell the watcher, and take the items it knows. */
void bu_hostonly(void)
{
	bu_callw(BU_WNAME, BU_WPATH, BU_WNAME, "RegisterStatusNotifierHost", K_IGNORE, 0, bu_hostname, 0);
	bu_callw(BU_WNAME, BU_WPATH, BU_PROPS, "Get", K_WITEMS, 0, BU_WNAME, "RegisteredStatusNotifierItems");
}

/* A reply to a message this sent: what it was for, acted on. */
void bu_reply(bu_msg *m)
{
	unsigned int rs = bu.mrserial(m);
	size_t i;
	bu_wait *w = 0;
	bu_item *t;
	bu_it it;
	int err = bu.mtype(m) == BU_ERROR;

	for (i = 0; i < bu_waits.n; i++)
		if (((bu_wait *)bu_waits.p[i])->serial == rs) {
			w = bu_waits.p[i];
			memmove(bu_waits.p + i, bu_waits.p + i + 1, (bu_waits.n - i - 1) * sizeof *bu_waits.p);
			bu_waits.n--;
			break;
		}
	if (!w)
		return;
	t = w->item ? bu_item_n(w->item) : 0;
	switch (w->kind) {
	case K_HELLO:
		if (!err && bu.iinit(m, &it) && bu_gets(&it)) {
			free(bu_me);
			bu_me = xs(bu_gets(&it));
			bu.setname(bu_c, bu_me);
		}
		break;
	case K_WATCH:
		if (!err && bu.iinit(m, &it) && bu_getn(&it, 0) == 1) {
			bu_role = 2;
			bu_ev("role watcher", -1);
			lg(HIBR_LDBG, "dbus: this desktop is the tray's watcher");
		} else {
			bu_role = 1;
			bu_hostonly();
			bu_ev("role host", -1);
			lg(HIBR_LDBG, "dbus: another program is the watcher; this desktop is a host");
		}
		break;
	case K_GETALL:
		if (!t)
			break;
		t->fetching = 0;
		if (err) {
			if (!t->known)
				bu_dropitem(t);
			break;
		}
		bu_props(t, m);
		bu_ev(t->known ? "change" : "add", t->n);
		t->known = 1;
		if (t->stale)
			bu_fetch(t);
		break;
	case K_OWNER:
		if (t && !err && bu.iinit(m, &it) && bu_gets(&it)) {
			free(t->owner);
			t->owner = xs(bu_gets(&it));
		}
		break;
	case K_LAYOUT:
		if (!t)
			break;
		t->mfetching = 0;
		if (err) {
			bu_ev("nomenu", t->n);
			break;
		}
		bu_layout(t, m);
		t->mknown = 1;
		bu_ev("menu", t->n);
		if (t->mstale) {
			t->mstale = 0;
			t->mfetching = 1;
		}
		break;
	case K_WITEMS:
		if (!err)
			bu_witems(m);
		break;
	case K_ACTIVATE:
		if (t && err)
			bu_ev("noactivate", t->n);
		break;
	}
	free(w);
}

/* Answer a call with nothing, or with an error when it is not one this
   answers -- a caller must never be left waiting on the desktop. */
void bu_answer(bu_msg *call, int ok)
{
	bu_msg *r = ok ? bu.mreturn(call) :
		bu.merror(call, "org.freedesktop.DBus.Error.UnknownMethod", "not here");

	bu_sendw(r, K_IGNORE, 0);
}

/* A variant holding one basic value. */
void bu_putvar(bu_it *it, int ty, const void *v)
{
	bu_it var;
	char sig[2] = { (char)ty, 0 };

	bu.iopen(it, 'v', sig, &var);
	bu.iappend(&var, ty, v);
	bu.iclose(it, &var);
}

/* The watcher's three properties, one or all, into a reply. */
void bu_wprops(bu_msg *r, const char *only)
{
	bu_it it, arr, de, var, sa;
	const char *k;
	size_t i;
	bu_bool yes = 1;
	int zero = 0;

	bu.iappinit(r, &it);
	if (only) {
		if (!strcmp(only, "RegisteredStatusNotifierItems")) {
			bu.iopen(&it, 'v', "as", &var);
			bu.iopen(&var, 'a', "s", &sa);
			for (i = 0; i < bu_items.n; i++)
				bu_putstr(&sa, ((bu_item *)bu_items.p[i])->key);
			bu.iclose(&var, &sa);
			bu.iclose(&it, &var);
		} else if (!strcmp(only, "IsStatusNotifierHostRegistered")) {
			bu_putvar(&it, 'b', &yes);
		} else {
			bu_putvar(&it, 'i', &zero);
		}
		return;
	}
	bu.iopen(&it, 'a', "{sv}", &arr);
	bu.iopen(&arr, 'e', 0, &de);
	k = "RegisteredStatusNotifierItems";
	bu_putstr(&de, k);
	bu.iopen(&de, 'v', "as", &var);
	bu.iopen(&var, 'a', "s", &sa);
	for (i = 0; i < bu_items.n; i++)
		bu_putstr(&sa, ((bu_item *)bu_items.p[i])->key);
	bu.iclose(&var, &sa);
	bu.iclose(&de, &var);
	bu.iclose(&arr, &de);
	bu.iopen(&arr, 'e', 0, &de);
	k = "IsStatusNotifierHostRegistered";
	bu_putstr(&de, k);
	bu_putvar(&de, 'b', &yes);
	bu.iclose(&arr, &de);
	bu.iopen(&arr, 'e', 0, &de);
	k = "ProtocolVersion";
	bu_putstr(&de, k);
	bu_putvar(&de, 'i', &zero);
	bu.iclose(&arr, &de);
	bu.iclose(&it, &arr);
}

/* A method call to this desktop. */
void bu_call(bu_msg *m)
{
	const char *ifc = bu.miface(m), *mem = bu.mmember(m), *path = bu.mpath(m);
	const char *a = 0, *b = 0;
	bu_it it;
	bu_msg *r, *sg;
	bu_item *t;

	if (!mem) {
		bu_answer(m, 0);
		return;
	}
	if (bu.iinit(m, &it)) {
		a = bu_gets(&it);
		if (bu.inext(&it))
			b = bu_gets(&it);
	}
	if (ifc && !strcmp(ifc, "org.freedesktop.DBus.Introspectable") && !strcmp(mem, "Introspect")) {
		static const char *xml =
			"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
			"\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
			"<node><interface name=\"org.kde.StatusNotifierWatcher\">"
			"<method name=\"RegisterStatusNotifierItem\"><arg name=\"service\" type=\"s\" direction=\"in\"/></method>"
			"<method name=\"RegisterStatusNotifierHost\"><arg name=\"service\" type=\"s\" direction=\"in\"/></method>"
			"<property name=\"RegisteredStatusNotifierItems\" type=\"as\" access=\"read\"/>"
			"<property name=\"IsStatusNotifierHostRegistered\" type=\"b\" access=\"read\"/>"
			"<property name=\"ProtocolVersion\" type=\"i\" access=\"read\"/>"
			"<signal name=\"StatusNotifierItemRegistered\"><arg type=\"s\"/></signal>"
			"<signal name=\"StatusNotifierItemUnregistered\"><arg type=\"s\"/></signal>"
			"<signal name=\"StatusNotifierHostRegistered\"/>"
			"</interface></node>\n";
		bu_it ri;

		if ((r = bu.mreturn(m))) {
			bu.iappinit(r, &ri);
			bu_putstr(&ri, xml);
			bu_sendw(r, K_IGNORE, 0);
		}
		return;
	}
	if (bu_role != 2 || !path || strcmp(path, BU_WPATH)) {
		bu_answer(m, 0);
		return;
	}
	if (ifc && !strcmp(ifc, BU_PROPS) && (!strcmp(mem, "Get") || !strcmp(mem, "GetAll"))) {
		if ((r = bu.mreturn(m))) {
			bu_wprops(r, !strcmp(mem, "Get") ? (b ? b : "") : 0);
			bu_sendw(r, K_IGNORE, 0);
		}
		return;
	}
	if (!strcmp(mem, "RegisterStatusNotifierItem")) {
		t = bu_additem(bu.msender(m), a);
		bu_answer(m, 1);
		if (t && (sg = bu.msignal(BU_WPATH, BU_WNAME, "StatusNotifierItemRegistered"))) {
			bu_it si;

			bu.iappinit(sg, &si);
			bu_putstr(&si, t->key);
			bu_sendw(sg, K_IGNORE, 0);
		}
		return;
	}
	if (!strcmp(mem, "RegisterStatusNotifierHost")) {
		bu_answer(m, 1);
		return;
	}
	bu_answer(m, 0);
}

/* Whether an item belongs to the sender of a signal. */
int bu_from(bu_item *t, const char *sender)
{
	return sender && ((t->owner && !strcmp(t->owner, sender)) || !strcmp(t->name, sender));
}

/* A signal on the bus. */
void bu_signal(bu_msg *m)
{
	const char *ifc = bu.miface(m), *mem = bu.mmember(m), *snd = bu.msender(m);
	const char *a = 0, *b = 0, *c = 0;
	bu_it it;
	size_t i;
	bu_item *t;

	if (!ifc || !mem)
		return;
	if (bu.iinit(m, &it)) {
		a = bu_gets(&it);
		if (bu.inext(&it)) {
			b = bu_gets(&it);
			if (bu.inext(&it))
				c = bu_gets(&it);
		}
	}
	if (!strcmp(ifc, BU_BUS) && !strcmp(mem, "NameOwnerChanged") && a && c && !*c) {
		for (i = bu_items.n; i > 0; i--) {
			t = bu_items.p[i - 1];
			if (!strcmp(t->name, a) || (t->owner && !strcmp(t->owner, a)))
				bu_dropitem(t);
		}
		if (!strcmp(a, BU_WNAME) && bu_role == 1)
			bu_request(BU_WNAME, K_WATCH);
		return;
	}
	if (!strcmp(ifc, BU_BUS) && !strcmp(mem, "NameOwnerChanged") && a && c && *c &&
	    !strcmp(a, BU_WNAME) && bu_role == 1) {
		bu_hostonly();
		return;
	}
	if (!strcmp(ifc, BU_WNAME) && bu_role == 1) {
		if (!strcmp(mem, "StatusNotifierItemRegistered") && a)
			bu_additem(0, a);
		else if (!strcmp(mem, "StatusNotifierItemUnregistered") && a && (t = bu_item_key(a)))
			bu_dropitem(t);
		return;
	}
	if (!strcmp(ifc, BU_IFACE)) {
		for (i = 0; i < bu_items.n; i++) {
			t = bu_items.p[i];
			if (bu_from(t, snd))
				bu_fetch(t);
		}
		return;
	}
	if (!strcmp(ifc, BU_MENU) && (!strcmp(mem, "LayoutUpdated") || !strcmp(mem, "ItemsPropertiesUpdated"))) {
		const char *p = bu.mpath(m);

		for (i = 0; i < bu_items.n; i++) {
			t = bu_items.p[i];
			if (bu_from(t, snd) && t->menu && p && !strcmp(t->menu, p) && t->mknown) {
				t->mknown = 0;
				bu_ev("menustale", t->n);
			}
		}
	}
	(void)b;
}

/* Every message: replies to what this sent, calls to it, and signals. */
int bu_filter(bu_conn *c, bu_msg *m, void *u)
{
	(void)c;
	(void)u;
	switch (bu.mtype(m)) {
	case BU_RET:
	case BU_ERROR:
		bu_reply(m);
		break;
	case BU_CALL:
		bu_call(m);
		break;
	case BU_SIGNAL:
		bu_signal(m);
		break;
	}
	return BU_HANDLED;
}

/* The session bus's address: DBUS_SESSION_BUS_ADDRESS, else the socket a
   systemd session keeps in the runtime folder; 0 when there is none, when
   the variable is set and empty (no bus, on purpose: the test harness),
   and never an autolaunch, which would start a bus of its own. */
char *bu_address(void)
{
	const char *a = getenv("DBUS_SESSION_BUS_ADDRESS"), *r = getenv("XDG_RUNTIME_DIR");
	struct stat st;
	str o;

	if (a)
		return *a && strncmp(a, "autolaunch:", 11) ? xs(a) : 0;
	if (!r || !*r)
		return 0;
	s_init(&o);
	s_cat(&o, r);
	s_cat(&o, "/bus");
	if (stat(o.p, &st) != 0 || !S_ISSOCK(st.st_mode)) {
		s_free(&o);
		return 0;
	}
	a = xs(o.p);
	o.n = 0;
	s_cat(&o, "unix:path=");
	s_cat(&o, a);
	free((char *)a);
	return o.p;
}

/* Whether a unix socket path in an address takes a connection at once: a
   bus whose queue is full would hold the connect libdbus makes, which has
   no timeout, so a full one is refused here first. */
int bu_reachable(const char *addr)
{
	const char *p = strstr(addr, "path=");
	struct sockaddr_un sa;
	size_t n;
	int fd, ok;

	if (!p || strncmp(addr, "unix:", 5))
		return 1;
	p += 5;
	n = strcspn(p, ",;");
	if (n >= sizeof sa.sun_path)
		return 0;
	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	memcpy(sa.sun_path, p, n);
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return 0;
	ok = connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0 || errno == EINPROGRESS;
	close(fd);
	return ok;
}

/* Close the connection and forget everything it knew. */
void bu_shut(void)
{
	size_t i;

	for (i = 0; i < bu_items.n; i++)
		bu_itemfree(bu_items.p[i]);
	bu_items.n = 0;
	for (i = 0; i < bu_waits.n; i++)
		free(bu_waits.p[i]);
	bu_waits.n = 0;
	if (bu_c) {
		bu.close(bu_c);
		bu.unref(bu_c);
	}
	bu_c = 0;
	bu_fd = -1;
	bu_role = 0;
	free(bu_me);
	bu_me = 0;
}

/* Connect and start the tray's two roles; 0 when there is no bus. */
int bu_open(void)
{
	bu_err e;
	char *addr;
	str nm;

	if (bu_c)
		return 1;
	if (!bu_load())
		return 0;
	if (!(addr = bu_address())) {
		lg(HIBR_LDBG, "dbus: no session bus");
		return 0;
	}
	if (!bu_reachable(addr)) {
		lg(HIBR_LERR, "dbus: the session bus at %s does not take a connection", addr);
		free(addr);
		return 0;
	}
	bu.sigpipe(0);
	memset(&e, 0, sizeof e);
	bu.err_init(&e);
	bu_c = bu.open(addr, &e);
	if (!bu_c) {
		lg(HIBR_LERR, "dbus: %s: %s", addr, e.message ? e.message : "cannot connect");
		bu.err_free(&e);
		free(addr);
		return 0;
	}
	bu.err_free(&e);
	free(addr);
	bu.exitdis(bu_c, 0);
	if (!bu.getfd(bu_c, &bu_fd))
		bu_fd = -1;
	if (bu_fd >= 0)
		fcntl(bu_fd, F_SETFD, fcntl(bu_fd, F_GETFD) | FD_CLOEXEC);
	bu.filter(bu_c, bu_filter, 0, 0);
	bu_callw(BU_BUS, BU_BUSPATH, BU_BUS, "Hello", K_HELLO, 0, 0, 0);
	bu_matches();
	s_init(&nm);
	s_cat(&nm, "org.kde.StatusNotifierHost-");
	s_num(&nm, (long)getpid());
	free(bu_hostname);
	bu_hostname = nm.p;
	bu_request(bu_hostname, K_IGNORE);
	bu_request(BU_WNAME, K_WATCH);
	bu_role = 1;
	bu.rw(bu_c, 0);
	lg(HIBR_LDBG, "dbus: connected, descriptor %d", bu_fd);
	return 1;
}

/* Read and write what is waiting, without waiting; dispatch everything. */
void bu_pump(void)
{
	time_t now = time(0);
	size_t i;
	bu_wait *w;
	bu_item *t;

	if (!bu_c)
		return;
	bu.rw(bu_c, 0);
	while (bu.dstatus(bu_c) == BU_DATA)
		bu.dispatch(bu_c);
	for (i = bu_waits.n; i > 0; i--) {
		w = bu_waits.p[i - 1];
		if (now - w->at < BU_WAITSEC)
			continue;
		if ((t = w->item ? bu_item_n(w->item) : 0)) {
			if (w->kind == K_GETALL)
				t->fetching = 0;
			if (w->kind == K_LAYOUT) {
				t->mfetching = 0;
				bu_ev("nomenu", t->n);
			}
			if (w->kind == K_GETALL && !t->known)
				bu_dropitem(t);
		}
		memmove(bu_waits.p + i - 1, bu_waits.p + i, (bu_waits.n - i) * sizeof *bu_waits.p);
		bu_waits.n--;
		free(w);
	}
	if (!bu.live(bu_c)) {
		lg(HIBR_LERR, "dbus: the session bus went away");
		for (i = 0; i < bu_items.n; i++)
			if (((bu_item *)bu_items.p[i])->known)
				bu_ev("gone", ((bu_item *)bu_items.p[i])->n);
		bu_shut();
		bu_ev("down", -1);
		return;
	}
	bu.rw(bu_c, 0);
	if (bu.tosend(bu_c))
		bu_ev("more", -1);
}

/* An item's menu asked for: AboutToShow first, so the program can bring
   it up to date, then its layout. */
void bu_menufetch(bu_item *t)
{
	bu_msg *m;
	bu_it it;
	int zero = 0, all = -1;

	if (!t->menu || !*t->menu)
		return;
	if (t->mfetching) {
		t->mstale = 1;
		return;
	}
	if ((m = bu.mcall(t->name, t->menu, BU_MENU, "AboutToShow"))) {
		bu.iappinit(m, &it);
		bu.iappend(&it, 'i', &zero);
		bu_sendw(m, K_IGNORE, 0);
	}
	if ((m = bu.mcall(t->name, t->menu, BU_MENU, "GetLayout"))) {
		bu_it arr;

		bu.iappinit(m, &it);
		bu.iappend(&it, 'i', &zero);
		bu.iappend(&it, 'i', &all);
		bu.iopen(&it, 'a', "s", &arr);
		bu.iclose(&it, &arr);
		t->mfetching = 1;
		bu_sendw(m, K_LAYOUT, t->n);
	}
}

/* A menu row clicked. */
void bu_click(bu_item *t, int id)
{
	bu_msg *m;
	bu_it it, var;
	const char *ev = "clicked";
	int zero = 0;
	unsigned int ts = (unsigned int)time(0);

	if (!t->menu || !(m = bu.mcall(t->name, t->menu, BU_MENU, "Event")))
		return;
	bu.iappinit(m, &it);
	bu.iappend(&it, 'i', &id);
	bu_putstr(&it, ev);
	bu.iopen(&it, 'v', "i", &var);
	bu.iappend(&var, 'i', &zero);
	bu.iclose(&it, &var);
	bu.iappend(&it, 'u', &ts);
	bu.mnoreply(m, 1);
	bu_sendw(m, K_IGNORE, 0);
}

/* Activate, SecondaryActivate or ContextMenu at a place, or Scroll. */
void bu_poke(bu_item *t, const char *what, int x, int y, const char *orient)
{
	bu_msg *m = bu.mcall(t->name, t->path, BU_IFACE, what);
	bu_it it;

	if (!m)
		return;
	bu.iappinit(m, &it);
	bu.iappend(&it, 'i', &x);
	if (orient)
		bu_putstr(&it, orient);
	else
		bu.iappend(&it, 'i', &y);
	bu_sendw(m, !strcmp(what, "Activate") ? K_ACTIVATE : K_IGNORE, t->n);
}

/* An item's picture as a PPM over a background, for `img draw`. */
int bu_ppm(bu_item *t, const char *path, const char *bg)
{
	unsigned br = 0, bgc = 0, bb = 0;
	FILE *f;
	long i, n;
	str tmp;

	if (!t->px)
		return 0;
	if (bg && bg[0] == '#' && strlen(bg) == 7)
		sscanf(bg + 1, "%2x%2x%2x", &br, &bgc, &bb);
	s_init(&tmp);
	s_cat(&tmp, path);
	s_cat(&tmp, ".part");
	f = fopen(tmp.p, "wb");
	if (!f) {
		s_free(&tmp);
		return 0;
	}
	fprintf(f, "P6\n%d %d\n255\n", t->pw, t->ph);
	n = (long)t->pw * t->ph;
	for (i = 0; i < n; i++) {
		const unsigned char *p = t->px + i * 4;
		unsigned a = p[0];
		unsigned char o[3];

		o[0] = (unsigned char)((p[1] * a + br * (255 - a)) / 255);
		o[1] = (unsigned char)((p[2] * a + bgc * (255 - a)) / 255);
		o[2] = (unsigned char)((p[3] * a + bb * (255 - a)) / 255);
		fwrite(o, 1, 3, f);
	}
	if (fclose(f) != 0 || rename(tmp.p, path) != 0) {
		unlink(tmp.p);
		s_free(&tmp);
		return 0;
	}
	s_free(&tmp);
	return 1;
}

/* Say text as the answer, or print it. */
int bu_say(sh *s, const char *t)
{
	if (s->bind)
		hibr_ret(s, t);
	else
		printf("%s\n", t);
	return HIBR_OK;
}

/* One field of a map answer. */
void bu_field(sh *s, const char *k1, const char *k2, const char *v)
{
	char *ks[2];

	if (s->bind) {
		ks[0] = (char *)k1;
		ks[1] = (char *)k2;
		hibr_setp(s, "RET", ks, k2 ? 2 : 1, v ? v : "");
	} else if (k2) {
		printf("%s\t%s\t%s\n", k1, k2, v ? v : "");
	} else {
		printf("%s\t%s\n", k1, v ? v : "");
	}
}

/* A list of lines as the answer, or printed. */
int bu_list(sh *s, vec *v)
{
	size_t i;

	if (s->bind) {
		if (v->n)
			hibr_retn(s, (char **)v->p, v->n);
		else
			hibr_retn(s, 0, 0);
	} else {
		for (i = 0; i < v->n; i++)
			printf("%s\n", (char *)v->p[i]);
	}
	return HIBR_OK;
}

/* One numbered field of a map answer: row k's field, a number. */
void bu_nfield(sh *s, long k, const char *f, long v)
{
	str a, b;

	s_init(&a);
	s_init(&b);
	s_num(&a, k);
	s_num(&b, v);
	bu_field(s, a.p, f, b.p);
	s_free(&a);
	s_free(&b);
}

/* One numbered field of a map answer, as text. */
void bu_tfield(sh *s, long k, const char *f, const char *v)
{
	str a;

	s_init(&a);
	s_num(&a, k);
	bu_field(s, a.p, f, v);
	s_free(&a);
}

/* Start a map answer. */
void bu_mapstart(sh *s)
{
	if (s->bind) {
		v_del(s, "RET");
		hibr_set(s, "RET", "", 0);
	}
}

/* The item a word names, or 0 with a message. */
bu_item *bu_arg(const char *a)
{
	bu_item *t = a ? bu_item_n(atoi(a)) : 0;

	if (!t || !t->known)
		lg(HIBR_LERR, "dbus: no such tray item: %s", a ? a : "");
	return t && t->known ? t : 0;
}

/* dbus open|fd|pump|items|item|menu|click|activate|secondary|context|scroll|pixmap|close */
int bu_bi(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	bu_item *t;
	vec v;
	size_t i;
	str o;
	int r;

	if (!strcmp(sub, "open")) {
		if (!bu_open())
			return HIBR_FAIL;
		s_init(&o);
		s_num(&o, bu_fd);
		bu_say(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "fd")) {
		if (!bu_c)
			return HIBR_FAIL;
		s_init(&o);
		s_num(&o, bu_fd);
		bu_say(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "pump")) {
		bu_pump();
		r = bu_list(s, &bu_evs);
		for (i = 0; i < bu_evs.n; i++)
			free(bu_evs.p[i]);
		bu_evs.n = 0;
		return r;
	}
	if (!strcmp(sub, "role"))
		return bu_say(s, bu_role == 2 ? "watcher" : bu_role == 1 ? "host" : "none");
	if (!strcmp(sub, "items")) {
		memset(&v, 0, sizeof v);
		for (i = 0; i < bu_items.n; i++) {
			t = bu_items.p[i];
			if (!t->known)
				continue;
			s_init(&o);
			s_num(&o, t->n);
			v_add(&v, o.p);
		}
		r = bu_list(s, &v);
		for (i = 0; i < v.n; i++)
			free(v.p[i]);
		free(v.p);
		return r;
	}
	if (!strcmp(sub, "close")) {
		bu_shut();
		return HIBR_OK;
	}
	if (ac < 3) {
		lg(HIBR_LERR, "usage: dbus open|fd|pump|role|items|close | item|menu|activate|secondary|context N [X Y] | scroll N DELTA vertical|horizontal | click N ID | pixmap N FILE [#rrggbb]");
		return 2;
	}
	if (!(t = bu_arg(av[2])))
		return HIBR_FAIL;
	if (!strcmp(sub, "item")) {
		bu_mapstart(s);
		bu_field(s, "name", 0, t->name);
		bu_field(s, "path", 0, t->path);
		bu_field(s, "id", 0, t->sid);
		bu_field(s, "title", 0, t->title);
		bu_field(s, "status", 0, t->status);
		bu_field(s, "category", 0, t->category);
		bu_field(s, "icon", 0, t->icon);
		bu_field(s, "iconpath", 0, t->iconpath);
		bu_field(s, "attention", 0, t->attention);
		bu_field(s, "tooltip", 0, t->tip);
		bu_field(s, "menu", 0, t->menu);
		bu_field(s, "ismenu", 0, t->ismenu ? "1" : "0");
		s_init(&o);
		if (t->px) {
			s_num(&o, t->pw);
			s_ch(&o, 'x');
			s_num(&o, t->ph);
		}
		bu_field(s, "pixmap", 0, o.p ? o.p : "");
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "menu")) {
		if (ac > 3 && !strcmp(av[3], "fetch")) {
			bu_menufetch(t);
			bu.rw(bu_c, 0);
			return t->menu && *t->menu ? HIBR_OK : HIBR_FAIL;
		}
		if (!t->mknown)
			return HIBR_FAIL;
		bu_mapstart(s);
		for (i = 0; i < (size_t)t->nrows; i++) {
			bu_row *rw = &t->rows[i];

			bu_nfield(s, (long)i, "id", rw->id);
			bu_nfield(s, (long)i, "depth", rw->depth);
			bu_tfield(s, (long)i, "label", rw->label);
			bu_nfield(s, (long)i, "sep", rw->sep);
			bu_nfield(s, (long)i, "enabled", rw->enabled);
			bu_tfield(s, (long)i, "toggle", rw->toggle == 1 ? "check" : rw->toggle == 2 ? "radio" : "");
			bu_nfield(s, (long)i, "on", rw->state == 1);
			bu_nfield(s, (long)i, "sub", rw->sub);
		}
		s_init(&o);
		s_num(&o, t->nrows);
		bu_field(s, "n", 0, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "click") && ac > 3) {
		bu_click(t, atoi(av[3]));
		bu.rw(bu_c, 0);
		return HIBR_OK;
	}
	if (!strcmp(sub, "activate") || !strcmp(sub, "secondary") || !strcmp(sub, "context")) {
		bu_poke(t, sub[0] == 'a' ? "Activate" : sub[0] == 's' ? "SecondaryActivate" : "ContextMenu",
			ac > 3 ? atoi(av[3]) : 0, ac > 4 ? atoi(av[4]) : 0, 0);
		bu.rw(bu_c, 0);
		return HIBR_OK;
	}
	if (!strcmp(sub, "scroll") && ac > 3) {
		bu_poke(t, "Scroll", atoi(av[3]), 0, ac > 4 && !strcmp(av[4], "horizontal") ? "horizontal" : "vertical");
		bu.rw(bu_c, 0);
		return HIBR_OK;
	}
	if (!strcmp(sub, "pixmap") && ac > 3)
		return bu_ppm(t, av[3], ac > 4 ? av[4] : 0) ? HIBR_OK : HIBR_FAIL;
	lg(HIBR_LERR, "dbus: no subcommand %s", sub);
	return 2;
}

/* Nothing until something opens the bus. */
int bu_ini(sh *s)
{
	(void)s;
	return HIBR_OK;
}

/* Close the bus when the module goes; libdbus stays loaded, since it keeps
   threads and handlers of its own a dlclose would leave dangling. */
void bu_fini(sh *s)
{
	size_t i;

	(void)s;
	bu_shut();
	for (i = 0; i < bu_evs.n; i++)
		free(bu_evs.p[i]);
	free(bu_evs.p);
	free(bu_items.p);
	free(bu_waits.p);
	memset(&bu_evs, 0, sizeof bu_evs);
	memset(&bu_items, 0, sizeof bu_items);
	memset(&bu_waits, 0, sizeof bu_waits);
	free(bu_hostname);
	bu_hostname = 0;
}

const hibr_bi bu_bis[] = {
	{ "dbus", bu_bi, "the session bus, for the desktop's tray: dbus open|pump|items|item|menu|click|activate|..." },
	HIBR_BI_END
};

HIBR_MODULE("dbus", "0.1", "the session bus for the desktop: the tray's watcher and host (StatusNotifierItem, dbusmenu)",
	    bu_bis, bu_ini, bu_fini);
