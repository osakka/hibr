#define _GNU_SOURCE

#include "ml.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

ml_conn *po_open(sh *s, ml_acct *a);
int po_list(sh *s, ml_conn *c);
int po_retr(ml_conn *c, const char *num, str *o);
int po_dele(ml_conn *c, const char *num);
void po_close(ml_conn *c);
int sm_send(sh *s, ml_acct *a, const char *raw, size_t n, int ac, char **rcpt);
void sm_addrs(const char *h, vec *out);

/* An open session: IMAP or POP3, and the account it is for. */
typedef struct ml_sess ml_sess;
struct ml_sess {
	int kind;
	ml_imap *im;
	ml_conn *pop;
};

vec ml_sessions;

/* Give back a word: into $RET when bound, else printed. */
void ml_say(sh *s, const char *v)
{
	hibr_ret(s, v);
	if (!s->bind)
		printf("%s\n", v);
}

/* Read a whole file. */
int ml_slurp(const char *path, str *o)
{
	char *b;
	ssize_t r;
	int fd = open(path, O_RDONLY);

	if (fd < 0) {
		lg(HIBR_LERR, "email: %s: %s", path, strerror(errno));
		return HIBR_FAIL;
	}
	b = xm(HIBR_IOCH);
	for (;;) {
		r = read(fd, b, HIBR_IOCH);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			break;
		s_add(o, b, (size_t)r);
		if (o->n > (size_t)ML_MAXMSG) {
			lg(HIBR_LERR, "email: %s is larger than a message may be", path);
			r = -1;
			break;
		}
	}
	free(b);
	close(fd);
	if (!o->p)
		s_cat(o, "");
	return r < 0 ? HIBR_FAIL : HIBR_OK;
}

/* Write a file whole, through a new one renamed into place. */
int ml_spill(const char *path, const char *p, size_t n)
{
	str tmp;
	int fd, rc = HIBR_OK;

	s_init(&tmp);
	s_cat(&tmp, path);
	s_cat(&tmp, ".part");
	fd = open(tmp.p, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0 || write(fd, p, n) != (ssize_t)n) {
		lg(HIBR_LERR, "email: cannot write %s: %s", path, strerror(errno));
		rc = HIBR_FAIL;
	}
	if (fd >= 0)
		close(fd);
	if (rc == HIBR_OK && rename(tmp.p, path) < 0) {
		lg(HIBR_LERR, "email: cannot write %s: %s", path, strerror(errno));
		rc = HIBR_FAIL;
	}
	s_free(&tmp);
	return rc;
}

/* The session a handle names, or none, said why. */
ml_sess *ml_sess_get(const char *h, int kind)
{
	long i = h ? strtol(h, 0, 10) : 0;
	ml_sess *x;

	if (i < 1 || (size_t)i > ml_sessions.n || !ml_sessions.p[i - 1]) {
		lg(HIBR_LERR, "email: %s: no such session", h ? h : "");
		return 0;
	}
	x = ml_sessions.p[i - 1];
	if (kind >= 0 && x->kind != kind) {
		lg(HIBR_LERR, "email: session %s is %s, and that needs %s", h,
		   x->kind == ML_POP ? "POP3" : "IMAP", kind == ML_POP ? "POP3" : "IMAP");
		return 0;
	}
	return x;
}

/* Split a decoded From into a name and an address. */
void ml_split(const char *v, str *name, str *addr)
{
	const char *lt = strrchr(v, '<'), *gt = lt ? strchr(lt, '>') : 0;
	const char *b, *e;

	ml_clr(name);
	ml_clr(addr);
	if (lt && gt) {
		s_add(addr, lt + 1, (size_t)(gt - lt - 1));
		b = v;
		e = lt;
	} else {
		s_cat(addr, v);
		b = v;
		e = v;
	}
	while (b < e && (isspace((unsigned char)*b) || *b == '"'))
		b++;
	while (e > b && (isspace((unsigned char)e[-1]) || e[-1] == '"'))
		e--;
	s_add(name, b, (size_t)(e - b));
	if (!name->n) {
		const char *at = strchr(addr->p ? addr->p : "", '@');

		s_add(name, addr->p ? addr->p : "", at ? (size_t)(at - addr->p) : addr->n);
	}
	if (!name->p)
		s_cat(name, "");
	if (!addr->p)
		s_cat(addr, "");
}

/* mail open ACCOUNT: log in, and give a handle to the session. */
int ml_open(sh *s, const char *name)
{
	ml_acct *a = ml_acctget(s, name);
	ml_sess *x;
	size_t i;
	str b;

	if (!a)
		return HIBR_FAIL;
	x = xm(sizeof *x);
	memset(x, 0, sizeof *x);
	x->kind = a->kind;
	if (a->kind == ML_POP)
		x->pop = po_open(s, a);
	else
		x->im = im_open(s, a);
	if (!x->pop && !x->im) {
		free(x);
		return HIBR_FAIL;
	}
	for (i = 0; i < ml_sessions.n && ml_sessions.p[i]; i++)
		;
	if (i == ml_sessions.n)
		v_add(&ml_sessions, x);
	else
		ml_sessions.p[i] = x;
	s_init(&b);
	s_num(&b, (long)i + 1);
	ml_say(s, b.p);
	s_free(&b);
	return HIBR_OK;
}

/* mail close h: log out. */
int ml_closecmd(const char *h)
{
	ml_sess *x = ml_sess_get(h, -1);

	if (!x)
		return HIBR_FAIL;
	if (x->im)
		im_close(x->im);
	if (x->pop)
		po_close(x->pop);
	ml_sessions.p[strtol(h, 0, 10) - 1] = 0;
	free(x);
	return HIBR_OK;
}

/* The role a folder plays, from its special-use flags or its name. */
const char *ml_role(const char *flags, const char *name)
{
	static const char *m[] = { "\\All", "all", "\\Archive", "archive", "\\Drafts", "drafts",
				   "\\Flagged", "starred", "\\Junk", "spam", "\\Sent", "sent",
				   "\\Trash", "trash", "\\Important", "important", 0 };
	int i;

	if (!strcasecmp(name, "INBOX"))
		return "inbox";
	for (i = 0; m[i]; i += 2)
		if (strcasestr(flags, m[i]))
			return m[i + 1];
	return "";
}

/* mail folders h: every folder, in r[i]["name"], ["flags"], ["delim"] and
   ["role"] -- inbox, sent, drafts, trash, spam, all, archive, starred,
   important -- names made UTF-8. */
int ml_folders(sh *s, ml_imap *m)
{
	size_t i, k = 0, j;
	char *ks[2];
	str num, nm, fl;

	if (im_cmd(m, "LIST \"\" \"*\"", 0) != HIBR_OK) {
		lg(HIBR_LERR, "email: LIST: %s", m->status.p ? m->status.p : "");
		return HIBR_FAIL;
	}
	s_init(&num);
	s_init(&nm);
	s_init(&fl);
	for (i = 0; i < m->untagged.n; i++) {
		const char *r = m->untagged.p[i];
		size_t at = 7;
		ml_iv *v;

		if (strncasecmp(r, "* LIST ", 7))
			continue;
		v = im_parse(r, strlen(r), &at);
		if (v->kids.n >= 3) {
			ml_iv *flags = v->kids.p[0], *dl = v->kids.p[1], *name = v->kids.p[2];

			ml_clr(&fl);
			for (j = 0; j < flags->kids.n; j++) {
				if (j)
					s_ch(&fl, ' ');
				s_cat(&fl, ((ml_iv *)flags->kids.p[j])->s.p);
			}
			if (!fl.p)
				s_cat(&fl, "");
			ml_clr(&nm);
			ml_mutf7dec(&nm, name->s.p ? name->s.p : "");
			if (!s->bind) {
				printf("%s\t%s\t%s\n", nm.p, ml_role(fl.p, name->s.p ? name->s.p : ""), fl.p);
			} else {
				ml_clr(&num);
				s_num(&num, (long)k);
				ks[0] = num.p;
				ks[1] = "name";
				hibr_setp(s, "RET", ks, 2, nm.p);
				ks[1] = "raw";
				hibr_setp(s, "RET", ks, 2, name->s.p ? name->s.p : "");
				ks[1] = "flags";
				hibr_setp(s, "RET", ks, 2, fl.p);
				ks[1] = "delim";
				hibr_setp(s, "RET", ks, 2, dl->t == 'n' ? "" : dl->s.p);
				ks[1] = "role";
				hibr_setp(s, "RET", ks, 2, ml_role(fl.p, name->s.p ? name->s.p : ""));
			}
			k++;
		}
		iv_free(v);
	}
	s_free(&num);
	s_free(&nm);
	s_free(&fl);
	return HIBR_OK;
}

/* A folder's name for a command: modified UTF-7, quoted. */
void ml_fname(str *o, const char *utf8)
{
	str e;

	s_init(&e);
	ml_mutf7enc(&e, utf8);
	im_quote(o, e.p ? e.p : "");
	s_free(&e);
}

/* mail select h folder [-r]: open a folder, read-only with -r; its
   uidvalidity, uidnext, exists and highestmodseq into $RET. */
int ml_select(sh *s, ml_imap *m, const char *folder, int ro)
{
	str cmd, v;
	size_t i;
	char *ks[1];
	const char *keys[] = { "UIDVALIDITY", "uidvalidity", "UIDNEXT", "uidnext", "HIGHESTMODSEQ",
			       "highestmodseq", 0 };

	s_init(&cmd);
	s_init(&v);
	s_cat(&cmd, ro ? "EXAMINE " : "SELECT ");
	ml_fname(&cmd, folder);
	if (im_cmd(m, cmd.p, 0) != HIBR_OK) {
		lg(HIBR_LERR, "email: cannot open %s: %s", folder, m->status.p ? m->status.p : "");
		s_free(&cmd);
		s_free(&v);
		return HIBR_FAIL;
	}
	free(m->folder);
	m->folder = xs(folder);
	for (i = 0; i < m->untagged.n; i++) {
		const char *r = m->untagged.p[i], *k;
		int j;

		if (strstr(r, " EXISTS")) {
			ks[0] = "exists";
			ml_clr(&v);
			s_num(&v, strtol(r + 2, 0, 10));
			if (s->bind)
				hibr_setp(s, "RET", ks, 1, v.p);
			else
				printf("exists\t%s\n", v.p);
		}
		for (j = 0; keys[j]; j += 2)
			if ((k = strcasestr(r, keys[j]))) {
				ks[0] = (char *)keys[j + 1];
				ml_clr(&v);
				s_num(&v, strtol(k + strlen(keys[j]), 0, 10));
				if (s->bind)
					hibr_setp(s, "RET", ks, 1, v.p);
				else
					printf("%s\t%s\n", keys[j + 1], v.p);
			}
	}
	s_free(&cmd);
	s_free(&v);
	return HIBR_OK;
}

/* Set r[uid][field], or keep it for printing. */
void ml_put(sh *s, const char *uid, const char *f, const char *v)
{
	char *ks[2];

	ks[0] = (char *)uid;
	ks[1] = (char *)f;
	hibr_setp(s, "RET", ks, 2, v ? v : "");
}

/* A FETCH reply's list of attributes, or none. */
ml_iv *ml_fetchlist(const char *r)
{
	const char *f = strcasestr(r, " FETCH ");
	size_t at;
	ml_iv *all, *l;

	if (strncmp(r, "* ", 2) || !f)
		return 0;
	at = (size_t)(f - r) + 7;
	all = im_parse(r, strlen(r), &at);
	if (!all->kids.n || ((ml_iv *)all->kids.p[0])->t != 'l') {
		iv_free(all);
		return 0;
	}
	l = all->kids.p[0];
	all->kids.n = 0;
	iv_free(all);
	return l;
}

/* A list value's words joined: flags, or Gmail labels made UTF-8. */
void ml_words(ml_iv *v, str *o, int utf7, char sep)
{
	size_t i;

	ml_clr(o);
	if (v)
		for (i = 0; i < v->kids.n; i++) {
			ml_iv *k = v->kids.p[i];

			if (i)
				s_ch(o, sep);
			if (utf7)
				ml_mutf7dec(o, k->s.p ? k->s.p : "");
			else
				s_cat(o, k->s.p ? k->s.p : "");
		}
	if (!o->p)
		s_cat(o, "");
}

/* mail headers h uids: the headers of messages by UID range or set, into
   r[uid][...]: from, fromname, fromaddr, to, cc, subject, date (epoch),
   size, flags, msgid, inreplyto, refs, attach, unsubscribe; and on Gmail
   labels (tab-separated), thrid and gmid. */
int ml_headers(sh *s, ml_imap *m, const char *set, int flagsonly)
{
	str cmd, hv, dv, nm, ad;
	size_t i;
	int rc = HIBR_OK;

	s_init(&cmd);
	s_init(&hv);
	s_init(&dv);
	s_init(&nm);
	s_init(&ad);
	s_cat(&cmd, "UID FETCH ");
	s_cat(&cmd, set);
	if (flagsonly)
		s_cat(&cmd, " (UID FLAGS");
	else
		s_cat(&cmd, " (UID FLAGS INTERNALDATE RFC822.SIZE BODY.PEEK[HEADER.FIELDS (DATE FROM TO "
			    "CC SUBJECT MESSAGE-ID IN-REPLY-TO REFERENCES CONTENT-TYPE LIST-UNSUBSCRIBE)]");
	if (m->gmail)
		s_cat(&cmd, flagsonly ? " X-GM-LABELS" : " X-GM-LABELS X-GM-THRID X-GM-MSGID");
	s_ch(&cmd, ')');
	if (im_cmd(m, cmd.p, 0) != HIBR_OK) {
		lg(HIBR_LERR, "email: FETCH: %s", m->status.p ? m->status.p : "");
		rc = HIBR_FAIL;
		goto out;
	}
	for (i = 0; i < m->untagged.n; i++) {
		ml_iv *l = ml_fetchlist(m->untagged.p[i]), *v;
		const char *uid;
		long d;

		if (!l)
			continue;
		v = iv_get(l, "UID");
		if (!v) {
			iv_free(l);
			continue;
		}
		uid = v->s.p;
		ml_words(iv_get(l, "FLAGS"), &hv, 0, ' ');
		if (s->bind)
			ml_put(s, uid, "flags", hv.p);
		if (m->gmail) {
			ml_words(iv_get(l, "X-GM-LABELS"), &dv, 1, '\t');
			if (s->bind)
				ml_put(s, uid, "labels", dv.p);
		}
		if (flagsonly) {
			if (!s->bind)
				printf("%s\t%s\t%s\n", uid, hv.p, m->gmail ? dv.p : "");
			iv_free(l);
			continue;
		}
		if ((v = iv_get(l, "BODY["))) {
			const char *h = v->s.p ? v->s.p : "";
			size_t hn = v->s.n;
			static const char *f[] = { "From", "from", "To", "to", "Cc", "cc", "Subject",
						   "subject", "Message-ID", "msgid", "In-Reply-To",
						   "inreplyto", "References", "refs",
						   "List-Unsubscribe", "unsubscribe", 0 };
			int k;

			for (k = 0; f[k]; k += 2) {
				ml_hraw(h, hn, f[k], &hv);
				ml_clr(&dv);
				ml_hdec(&dv, hv.p ? hv.p : "", hv.n);
				if (s->bind)
					ml_put(s, uid, f[k + 1], dv.p);
				if (k == 0) {
					ml_split(dv.p, &nm, &ad);
					if (s->bind) {
						ml_put(s, uid, "fromname", nm.p);
						ml_put(s, uid, "fromaddr", ad.p);
					}
				}
				if (k == 6 && !s->bind) {
					str dt;

					s_init(&dt);
					ml_hraw(h, hn, "Date", &dt);
					printf("%s\t%ld\t%s\t%s\n", uid, ml_date(dt.p), nm.p, dv.p);
					s_free(&dt);
				}
			}
			ml_hraw(h, hn, "Date", &hv);
			d = ml_date(hv.p);
			if (!d && (v = iv_get(l, "INTERNALDATE")))
				d = ml_date(v->s.p);
			ml_clr(&dv);
			s_num(&dv, d);
			if (s->bind)
				ml_put(s, uid, "date", dv.p);
			ml_hraw(h, hn, "Content-Type", &hv);
			if (s->bind)
				ml_put(s, uid, "attach",
				       hv.p && (strcasestr(hv.p, "multipart/mixed") ||
						strcasestr(hv.p, "application/")) ? "1" : "0");
		}
		if (s->bind) {
			if ((v = iv_get(l, "RFC822.SIZE")))
				ml_put(s, uid, "size", v->s.p);
			if ((v = iv_get(l, "X-GM-THRID")))
				ml_put(s, uid, "thrid", v->s.p);
			if ((v = iv_get(l, "X-GM-MSGID")))
				ml_put(s, uid, "gmid", v->s.p);
		}
		iv_free(l);
	}
out:
	s_free(&cmd);
	s_free(&hv);
	s_free(&dv);
	s_free(&nm);
	s_free(&ad);
	return rc;
}

/* mail uids h: every UID in the open folder, as an array. */
int ml_uids(sh *s, ml_imap *m)
{
	size_t i;
	vec out = { 0, 0, 0 };

	if (im_cmd(m, "UID SEARCH ALL", 0) != HIBR_OK) {
		lg(HIBR_LERR, "email: SEARCH: %s", m->status.p ? m->status.p : "");
		return HIBR_FAIL;
	}
	for (i = 0; i < m->untagged.n; i++) {
		char *r = m->untagged.p[i], *t, *sv = 0;

		if (strncasecmp(r, "* SEARCH", 8))
			continue;
		for (t = strtok_r(r + 8, " \r\n", &sv); t; t = strtok_r(0, " \r\n", &sv))
			v_add(&out, t);
	}
	if (s->bind)
		hibr_retn(s, (char **)out.p, out.n);
	else
		for (i = 0; i < out.n; i++)
			printf("%s\n", (char *)out.p[i]);
	v_free(&out);
	return HIBR_OK;
}

/* mail fetch h uid file: a whole message, as the server has it, into a
   file -- by UID for IMAP, by number for POP3. */
int ml_fetch(sh *s, ml_sess *x, const char *uid, const char *file)
{
	str cmd, body;
	size_t i;
	int rc = HIBR_FAIL;

	(void)s;
	s_init(&body);
	if (x->pop) {
		if (po_retr(x->pop, uid, &body) == HIBR_OK)
			rc = ml_spill(file, body.p ? body.p : "", body.n);
		s_free(&body);
		return rc;
	}
	s_init(&cmd);
	s_cat(&cmd, "UID FETCH ");
	s_cat(&cmd, uid);
	s_cat(&cmd, " (UID BODY.PEEK[])");
	if (im_cmd(x->im, cmd.p, 0) != HIBR_OK) {
		lg(HIBR_LERR, "email: FETCH %s: %s", uid, x->im->status.p ? x->im->status.p : "");
		goto out;
	}
	for (i = 0; i < x->im->untagged.n; i++) {
		ml_iv *l = ml_fetchlist(x->im->untagged.p[i]), *v;

		if (!l)
			continue;
		if ((v = iv_get(l, "BODY[")) && iv_get(l, "UID") &&
		    !strcmp(iv_get(l, "UID")->s.p, uid)) {
			rc = ml_spill(file, v->s.p ? v->s.p : "", v->s.n);
			iv_free(l);
			goto out;
		}
		iv_free(l);
	}
	lg(HIBR_LERR, "email: %s: no such message", uid);
out:
	s_free(&cmd);
	s_free(&body);
	return rc;
}

/* mail store h uids +flag... -flag...: set or clear flags; mail label h
   uids +label -label: Gmail's labels. */
int ml_store(ml_imap *m, int ac, char **av, int labels)
{
	str add, rm, cmd, e;
	int i, rc = HIBR_OK;

	if (labels && !m->gmail) {
		lg(HIBR_LERR, "email: labels are Gmail's; this server has folders");
		return HIBR_FAIL;
	}
	s_init(&add);
	s_init(&rm);
	s_init(&cmd);
	s_init(&e);
	for (i = 4; i < ac; i++) {
		str *o = av[i][0] == '-' ? &rm : &add;
		const char *w = av[i][0] == '-' || av[i][0] == '+' ? av[i] + 1 : av[i];

		if (o->n)
			s_ch(o, ' ');
		if (labels && w[0] != '\\') {
			ml_clr(&e);
			ml_mutf7enc(&e, w);
			im_quote(o, e.p ? e.p : "");
		} else {
			s_cat(o, w);
		}
	}
	if (add.n) {
		s_cat(&cmd, "UID STORE ");
		s_cat(&cmd, av[3]);
		s_cat(&cmd, labels ? " +X-GM-LABELS.SILENT (" : " +FLAGS.SILENT (");
		s_cat(&cmd, add.p);
		s_ch(&cmd, ')');
		if (im_cmd(m, cmd.p, 0) != HIBR_OK)
			rc = HIBR_FAIL;
	}
	if (rm.n && rc == HIBR_OK) {
		ml_clr(&cmd);
		s_cat(&cmd, "UID STORE ");
		s_cat(&cmd, av[3]);
		s_cat(&cmd, labels ? " -X-GM-LABELS.SILENT (" : " -FLAGS.SILENT (");
		s_cat(&cmd, rm.p);
		s_ch(&cmd, ')');
		if (im_cmd(m, cmd.p, 0) != HIBR_OK)
			rc = HIBR_FAIL;
	}
	if (rc != HIBR_OK)
		lg(HIBR_LERR, "email: STORE: %s", m->status.p ? m->status.p : "");
	s_free(&add);
	s_free(&rm);
	s_free(&cmd);
	s_free(&e);
	return rc;
}

/* mail move h uids folder, mail copy h uids folder: MOVE where the server
   has it, else COPY and then the originals deleted and expunged. */
int ml_move(ml_imap *m, const char *set, const char *folder, int move)
{
	str cmd;
	int rc;

	s_init(&cmd);
	s_cat(&cmd, move && m->move ? "UID MOVE " : "UID COPY ");
	s_cat(&cmd, set);
	s_ch(&cmd, ' ');
	ml_fname(&cmd, folder);
	rc = im_cmd(m, cmd.p, 0);
	if (rc == HIBR_OK && move && !m->move) {
		ml_clr(&cmd);
		s_cat(&cmd, "UID STORE ");
		s_cat(&cmd, set);
		s_cat(&cmd, " +FLAGS.SILENT (\\Deleted)");
		rc = im_cmd(m, cmd.p, 0);
		if (rc == HIBR_OK) {
			ml_clr(&cmd);
			if (m->uidplus) {
				s_cat(&cmd, "UID EXPUNGE ");
				s_cat(&cmd, set);
			} else {
				s_cat(&cmd, "EXPUNGE");
			}
			rc = im_cmd(m, cmd.p, 0);
		}
	}
	if (rc != HIBR_OK)
		lg(HIBR_LERR, "email: %s: %s", move ? "MOVE" : "COPY", m->status.p ? m->status.p : "");
	s_free(&cmd);
	return rc;
}

/* mail append h folder file [flag...]: put a message in a folder -- a
   sent copy, a draft. */
int ml_append(ml_imap *m, int ac, char **av)
{
	str cmd, body;
	int i, rc;

	s_init(&body);
	if (ml_slurp(av[4], &body) != HIBR_OK) {
		s_free(&body);
		return HIBR_FAIL;
	}
	s_init(&cmd);
	s_cat(&cmd, "APPEND ");
	ml_fname(&cmd, av[3]);
	s_cat(&cmd, " (");
	for (i = 5; i < ac; i++) {
		if (i > 5)
			s_ch(&cmd, ' ');
		s_cat(&cmd, av[i]);
	}
	s_cat(&cmd, ") ");
	rc = im_cmd(m, cmd.p, &body);
	if (rc != HIBR_OK)
		lg(HIBR_LERR, "email: APPEND: %s", m->status.p ? m->status.p : "");
	s_free(&cmd);
	s_free(&body);
	return rc;
}

/* mail idle h seconds: wait for the open folder to change, with IDLE
   where the server has it, else by asking every half minute. Status 0
   when something changed -- what, in $RET -- 1 when the time ran out. */
int ml_idle(sh *s, ml_imap *m, int secs)
{
	str cmd, line, ev;
	time_t until = time(0) + (secs > 0 ? secs : 1500);
	int changed = 0, rc;
	long tag;

	s_init(&line);
	s_init(&ev);
	if (!m->idle) {
		while (time(0) < until && !changed) {
			sleep(until - time(0) > 30 ? 30 : (unsigned)(until - time(0)));
			if (im_cmd(m, "NOOP", 0) != HIBR_OK)
				return 2;
			changed = m->untagged.n > 0;
		}
		hibr_ret(s, changed ? "changed" : "");
		s_free(&line);
		s_free(&ev);
		return changed ? HIBR_OK : HIBR_FAIL;
	}
	s_init(&cmd);
	tag = ++m->tag;
	s_ch(&cmd, 'T');
	s_num(&cmd, tag);
	s_cat(&cmd, " IDLE\r\n");
	if (ml_wall(m->c, cmd.p, cmd.n) != HIBR_OK || ml_line(m->c, &line) != HIBR_OK ||
	    line.p[0] != '+') {
		lg(HIBR_LERR, "email: IDLE: %s", line.p ? line.p : "no reply");
		s_free(&cmd);
		s_free(&line);
		s_free(&ev);
		return 2;
	}
	while (!changed) {
		long left = (long)(until - time(0));

		if (left <= 0)
			break;
		if (!ml_ready(m->c, (int)(left > 60 ? 60000 : left * 1000)))
			continue;
		if (ml_line(m->c, &line) != HIBR_OK) {
			s_free(&cmd);
			s_free(&line);
			s_free(&ev);
			return 2;
		}
		if (strstr(line.p, "EXISTS") || strstr(line.p, "EXPUNGE") || strstr(line.p, "FETCH")) {
			changed = 1;
			s_cat(&ev, line.p);
		}
	}
	rc = ml_wall(m->c, "DONE\r\n", 6);
	ml_clr(&cmd);
	s_ch(&cmd, 'T');
	s_num(&cmd, tag);
	while (rc == HIBR_OK && ml_line(m->c, &line) == HIBR_OK) {
		if (!strncmp(line.p, cmd.p, cmd.n) && line.p[cmd.n] == ' ')
			break;
		if (strstr(line.p, "EXISTS") || strstr(line.p, "EXPUNGE") || strstr(line.p, "FETCH"))
			changed = 1;
	}
	hibr_ret(s, changed ? (ev.p ? ev.p : "changed") : "");
	s_free(&cmd);
	s_free(&line);
	s_free(&ev);
	return changed ? HIBR_OK : HIBR_FAIL;
}

/* mail parse file: a message's decoded headers -- from, fromname,
   fromaddr, to, cc, replyto, subject, date, msgid, inreplyto, refs --
   its text and HTML bodies as UTF-8, and parts[i] with type, name, size,
   cid and disp. */
int ml_parsecmd(sh *s, const char *file)
{
	str raw, k, nm, ad;
	ml_msg m;
	size_t i;
	char *ks[3];
	static const char *f[] = { "From", "from", "To", "to", "Cc", "cc", "Reply-To", "replyto",
				   "Subject", "subject", "Message-ID", "msgid", "In-Reply-To",
				   "inreplyto", "References", "refs", "List-Unsubscribe",
				   "unsubscribe", 0 };
	int j;

	s_init(&raw);
	if (ml_slurp(file, &raw) != HIBR_OK) {
		s_free(&raw);
		return HIBR_FAIL;
	}
	ml_msgparse(raw.p, raw.n, &m);
	s_init(&k);
	s_init(&nm);
	s_init(&ad);
	for (j = 0; f[j]; j += 2) {
		const char *v = ml_hget(&m, f[j]);

		ks[0] = (char *)f[j + 1];
		if (s->bind)
			hibr_setp(s, "RET", ks, 1, v ? v : "");
		else if (v)
			printf("%s: %s\n", f[j], v);
		if (j == 0) {
			ml_split(v ? v : "", &nm, &ad);
			if (s->bind) {
				ks[0] = "fromname";
				hibr_setp(s, "RET", ks, 1, nm.p);
				ks[0] = "fromaddr";
				hibr_setp(s, "RET", ks, 1, ad.p);
			}
		}
	}
	ml_clr(&k);
	s_num(&k, ml_date(ml_hget(&m, "Date")));
	if (s->bind) {
		ks[0] = "date";
		hibr_setp(s, "RET", ks, 1, k.p);
		ks[0] = "text";
		hibr_setp(s, "RET", ks, 1, m.text.p ? m.text.p : "");
		ks[0] = "html";
		hibr_setp(s, "RET", ks, 1, m.html.p ? m.html.p : "");
		for (i = 0; i < m.parts.n; i++) {
			ml_part *p = m.parts.p[i];
			str num, sz;

			s_init(&num);
			s_init(&sz);
			s_num(&num, (long)i);
			s_num(&sz, (long)(p->be - p->bs));
			ks[0] = "parts";
			ks[1] = num.p;
			ks[2] = "type";
			hibr_setp(s, "RET", ks, 3, p->type);
			ks[2] = "name";
			hibr_setp(s, "RET", ks, 3, p->name ? p->name : "");
			ks[2] = "size";
			hibr_setp(s, "RET", ks, 3, sz.p);
			ks[2] = "cid";
			hibr_setp(s, "RET", ks, 3, p->cid ? p->cid : "");
			ks[2] = "disp";
			hibr_setp(s, "RET", ks, 3, p->disp ? p->disp : "");
			s_free(&num);
			s_free(&sz);
		}
	} else {
		printf("\n%s", m.text.p ? m.text.p : "");
		for (i = 0; i < m.parts.n; i++) {
			ml_part *p = m.parts.p[i];

			printf("[part %lu: %s%s%s, %lu bytes]\n", (unsigned long)i, p->type,
			       p->name ? " " : "", p->name ? p->name : "", (unsigned long)(p->be - p->bs));
		}
	}
	ml_msgfree(&m);
	s_free(&raw);
	s_free(&k);
	s_free(&nm);
	s_free(&ad);
	return HIBR_OK;
}

/* mail part file i out: one part of a message, decoded, into a file. */
int ml_partcmd(const char *file, const char *idx, const char *out)
{
	str raw, body;
	ml_msg m;
	int rc;

	s_init(&raw);
	if (ml_slurp(file, &raw) != HIBR_OK) {
		s_free(&raw);
		return HIBR_FAIL;
	}
	ml_msgparse(raw.p, raw.n, &m);
	s_init(&body);
	rc = ml_partbody(&m, (size_t)strtoul(idx, 0, 10), &body);
	if (rc == HIBR_OK)
		rc = ml_spill(out, body.p ? body.p : "", body.n);
	else
		lg(HIBR_LERR, "email: %s has no part %s", file, idx);
	ml_msgfree(&m);
	s_free(&raw);
	s_free(&body);
	return rc;
}

/* An address list for a header: each display name encoded when it is not
   plain ASCII, the addresses kept as they are. */
void ml_addrenc(str *o, const char *list)
{
	const char *p = list, *st = list;
	int q = 0, ang = 0, first = 1;

	for (;; p++) {
		if (*p == '"')
			q = !q;
		if (!q && *p == '<')
			ang = 1;
		if (!q && *p == '>')
			ang = 0;
		if (*p && (q || ang || *p != ','))
			continue;
		{
			str one, nm;
			const char *lt, *b, *e;

			s_init(&one);
			s_init(&nm);
			s_add(&one, st, (size_t)(p - st));
			lt = strrchr(one.p ? one.p : "", '<');
			if (one.n) {
				if (!first)
					s_cat(o, ", ");
				first = 0;
				if (lt) {
					b = one.p;
					e = lt;
					while (b < e && (isspace((unsigned char)*b) || *b == '"'))
						b++;
					while (e > b && (isspace((unsigned char)e[-1]) || e[-1] == '"'))
						e--;
					s_add(&nm, b, (size_t)(e - b));
					if (nm.n) {
						str enc;

						s_init(&enc);
						ml_henc(&enc, nm.p);
						if (!strcmp(enc.p, nm.p) && strpbrk(nm.p, ",.;:@()[]<>\\")) {
							s_ch(o, '"');
							s_cat(o, nm.p);
							s_ch(o, '"');
						} else {
							s_cat(o, enc.p);
						}
						s_free(&enc);
						s_ch(o, ' ');
					}
					s_cat(o, lt);
				} else {
					b = one.p;
					e = one.p + one.n;
					while (b < e && isspace((unsigned char)*b))
						b++;
					while (e > b && isspace((unsigned char)e[-1]))
						e--;
					s_add(o, b, (size_t)(e - b));
				}
			}
			s_free(&one);
			s_free(&nm);
		}
		if (!*p)
			break;
		st = p + 1;
	}
}

/* A content type guessed from a file's name. */
const char *ml_ctype(const char *f)
{
	static const char *m[] = { ".pdf", "application/pdf", ".png", "image/png", ".jpg", "image/jpeg",
				   ".jpeg", "image/jpeg", ".gif", "image/gif", ".txt", "text/plain",
				   ".html", "text/html", ".htm", "text/html", ".zip", "application/zip",
				   ".gz", "application/gzip", ".json", "application/json", ".csv",
				   "text/csv", ".md", "text/markdown", ".svg", "image/svg+xml",
				   ".mp3", "audio/mpeg", ".mp4", "video/mp4", ".eml", "message/rfc822",
				   ".doc", "application/msword", ".xls", "application/vnd.ms-excel",
				   ".docx",
				   "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
				   ".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
				   0 };
	const char *dot = strrchr(f, '.');
	int i;

	for (i = 0; dot && m[i]; i += 2)
		if (!strcasecmp(dot, m[i]))
			return m[i + 1];
	return "application/octet-stream";
}

/* A text body part: plain ASCII as it is, anything else quoted-printable,
   lines ending CRLF. */
void ml_textpart(str *o, const char *type, const char *p, size_t n)
{
	size_t i;
	int plain = 1;

	for (i = 0; i < n; i++)
		if ((unsigned char)p[i] >= 0x80)
			plain = 0;
	s_cat(o, "Content-Type: ");
	s_cat(o, type);
	s_cat(o, "; charset=UTF-8\r\n");
	if (plain) {
		s_cat(o, "Content-Transfer-Encoding: 7bit\r\n\r\n");
		for (i = 0; i < n; i++) {
			if (p[i] == '\n')
				s_cat(o, "\r\n");
			else if (p[i] != '\r')
				s_ch(o, p[i]);
		}
		if (n && p[n - 1] != '\n')
			s_cat(o, "\r\n");
	} else {
		s_cat(o, "Content-Transfer-Encoding: quoted-printable\r\n\r\n");
		ml_qpenc(o, p, n);
		s_cat(o, "\r\n");
	}
}

/* A boundary no body will hold. */
void ml_boundary(str *o, int k)
{
	static long seq;

	s_cat(o, "=_hibr_");
	s_num(o, (long)time(0));
	s_ch(o, '_');
	s_num(o, (long)getpid());
	s_ch(o, '_');
	s_num(o, ++seq * 7 + k);
}

/* mail build out -f from [-t to] [-c cc] [-b bcc] [-s subject]
   [-T textfile] [-H htmlfile] [-a file]... [-r in-reply-to]
   [-R references]: write a message ready to send; its Message-ID is the
   result. A Bcc header is written for mail send to read, and never sent. */
int ml_build(sh *s, int ac, char **av)
{
	const char *out = av[2], *from = 0, *to = 0, *cc = 0, *bcc = 0, *subj = "", *tf = 0,
		   *hf = 0, *irt = 0, *refs = 0, *cf = 0, *method = "REQUEST";
	vec att = { 0, 0, 0 };
	str o, text, html, b1, b2, mid, tmp, cal, ct;
	int i, rc;
	str date;
	time_t now = time(0);
	struct tm lt;

	for (i = 3; i + 1 < ac; i += 2) {
		const char *k = av[i], *v = av[i + 1];

		if (!strcmp(k, "-f"))
			from = v;
		else if (!strcmp(k, "-t"))
			to = v;
		else if (!strcmp(k, "-c"))
			cc = v;
		else if (!strcmp(k, "-b"))
			bcc = v;
		else if (!strcmp(k, "-s"))
			subj = v;
		else if (!strcmp(k, "-T"))
			tf = v;
		else if (!strcmp(k, "-H"))
			hf = v;
		else if (!strcmp(k, "-a"))
			v_add(&att, (void *)v);
		else if (!strcmp(k, "-r"))
			irt = v;
		else if (!strcmp(k, "-R"))
			refs = v;
		else if (!strcmp(k, "-C"))
			cf = v;
		else if (!strcmp(k, "-M"))
			method = v;
		else {
			lg(HIBR_LERR, "email: build: %s is not an option", k);
			v_free(&att);
			return 2;
		}
	}
	if (!from) {
		lg(HIBR_LERR, "usage: email build out -f from [-t to] [-c cc] [-b bcc] [-s subject] [-C calendar-file [-M method]] "
			      "[-T textfile] [-H htmlfile] [-a file]... [-r in-reply-to] [-R refs]");
		v_free(&att);
		return 2;
	}
	s_init(&o);
	s_init(&text);
	s_init(&html);
	s_init(&b1);
	s_init(&b2);
	s_init(&mid);
	s_init(&tmp);
	s_init(&cal);
	s_init(&ct);
	if ((tf && ml_slurp(tf, &text) != HIBR_OK) || (hf && ml_slurp(hf, &html) != HIBR_OK) ||
	    (cf && ml_slurp(cf, &cal) != HIBR_OK)) {
		rc = HIBR_FAIL;
		goto out;
	}
	localtime_r(&now, &lt);
	s_init(&date);
	s_grow(&date, 64);
	date.n = strftime(date.p, 64, "%a, %d %b %Y %H:%M:%S %z", &lt);
	{
		vec fa = { 0, 0, 0 };
		const char *at;
		size_t k;

		sm_addrs(from, &fa);
		at = fa.n ? strchr(fa.p[0], '@') : 0;
		s_ch(&mid, '<');
		s_num(&mid, (long)now);
		s_ch(&mid, '.');
		s_num(&mid, (long)getpid());
		s_ch(&mid, '.');
		s_num(&mid, (long)(rand() & 0xFFFFFF));
		s_cat(&mid, at ? at : "@hibr.local");
		s_ch(&mid, '>');
		for (k = 0; k < fa.n; k++)
			free(fa.p[k]);
		v_free(&fa);
	}
	s_cat(&o, "Date: ");
	s_cat(&o, date.p);
	s_free(&date);
	s_cat(&o, "\r\nFrom: ");
	ml_addrenc(&o, from);
	if (to && *to) {
		s_cat(&o, "\r\nTo: ");
		ml_addrenc(&o, to);
	}
	if (cc && *cc) {
		s_cat(&o, "\r\nCc: ");
		ml_addrenc(&o, cc);
	}
	if (bcc && *bcc) {
		s_cat(&o, "\r\nBcc: ");
		ml_addrenc(&o, bcc);
	}
	s_cat(&o, "\r\nSubject: ");
	ml_henc(&o, subj);
	s_cat(&o, "\r\nMessage-ID: ");
	s_cat(&o, mid.p);
	if (irt && *irt) {
		s_cat(&o, "\r\nIn-Reply-To: ");
		s_cat(&o, irt);
	}
	if (refs && *refs) {
		s_cat(&o, "\r\nReferences: ");
		s_cat(&o, refs);
	}
	s_cat(&o, "\r\nMIME-Version: 1.0\r\nUser-Agent: hibr mail\r\n");
	if (att.n) {
		ml_boundary(&b1, 1);
		s_cat(&o, "Content-Type: multipart/mixed; boundary=\"");
		s_cat(&o, b1.p);
		s_cat(&o, "\"\r\n\r\nThis is a message in MIME format.\r\n--");
		s_cat(&o, b1.p);
		s_cat(&o, "\r\n");
	}
	if (html.n || cal.n) {
		ml_boundary(&b2, 2);
		s_cat(&o, "Content-Type: multipart/alternative; boundary=\"");
		s_cat(&o, b2.p);
		s_cat(&o, "\"\r\n\r\n--");
		s_cat(&o, b2.p);
		s_cat(&o, "\r\n");
		ml_textpart(&o, "text/plain", text.p ? text.p : "", text.n);
		s_cat(&o, "--");
		s_cat(&o, b2.p);
		s_cat(&o, "\r\n");
		if (html.n) {
			ml_textpart(&o, "text/html", html.p, html.n);
			s_cat(&o, "--");
			s_cat(&o, b2.p);
			s_cat(&o, "\r\n");
		}
		if (cal.n) {
			s_cat(&ct, "text/calendar; method=");
			s_cat(&ct, method);
			ml_textpart(&o, ct.p, cal.p, cal.n);
			s_cat(&o, "--");
			s_cat(&o, b2.p);
			s_cat(&o, "\r\n");
			lg(HIBR_LDBG, "email: build: a calendar part, method %s", method);
		}
		o.n -= 2;
		s_cat(&o, "--\r\n");
	} else {
		ml_textpart(&o, "text/plain", text.p ? text.p : "", text.n);
	}
	for (i = 0; i < (int)att.n; i++) {
		const char *f = att.p[i], *base = strrchr(f, '/');
		size_t k;
		int plain = 1;

		base = base ? base + 1 : f;
		ml_clr(&tmp);
		if (ml_slurp(f, &tmp) != HIBR_OK) {
			rc = HIBR_FAIL;
			goto out;
		}
		for (k = 0; base[k]; k++)
			if ((unsigned char)base[k] >= 0x80 || base[k] == '"' || base[k] == '\\')
				plain = 0;
		s_cat(&o, "--");
		s_cat(&o, b1.p);
		s_cat(&o, "\r\nContent-Type: ");
		s_cat(&o, ml_ctype(base));
		s_cat(&o, "\r\nContent-Transfer-Encoding: base64\r\nContent-Disposition: attachment; ");
		if (plain) {
			s_cat(&o, "filename=\"");
			s_cat(&o, base);
			s_ch(&o, '"');
		} else {
			static const char hx[] = "0123456789ABCDEF";

			s_cat(&o, "filename*=UTF-8''");
			for (k = 0; base[k]; k++) {
				unsigned char c = (unsigned char)base[k];

				if (isalnum(c) || strchr(".-_", c)) {
					s_ch(&o, (char)c);
				} else {
					s_ch(&o, '%');
					s_ch(&o, hx[c >> 4]);
					s_ch(&o, hx[c & 15]);
				}
			}
		}
		s_cat(&o, "\r\n\r\n");
		ml_b64enc(&o, tmp.p, tmp.n);
	}
	if (att.n) {
		s_cat(&o, "--");
		s_cat(&o, b1.p);
		s_cat(&o, "--\r\n");
	}
	rc = ml_spill(out, o.p, o.n);
	if (rc == HIBR_OK)
		ml_say(s, mid.p);
out:
	v_free(&att);
	s_free(&o);
	s_free(&text);
	s_free(&html);
	s_free(&cal);
	s_free(&ct);
	s_free(&b1);
	s_free(&b2);
	s_free(&mid);
	s_free(&tmp);
	return rc;
}

/* email: accounts, IMAP with Gmail's labels and IDLE, POP3, sending over
   SMTP, and messages parsed and built. */
int m_mail(sh *s, int ac, char **av)
{
	const char *sub = ac >= 2 ? av[1] : "";
	ml_sess *x;

	if (!strcmp(sub, "account") && ac >= 4 && !strcmp(av[2], "set"))
		return ml_acctset(s, ac, av);
	if (!strcmp(sub, "account") && ac >= 4 && !strcmp(av[2], "rm"))
		return ml_acctrm(s, av[3]);
	if (!strcmp(sub, "account") && ac >= 5 && !strcmp(av[2], "mv"))
		return ml_acctmv(s, av[3], av[4]);
	if (!strcmp(sub, "accounts"))
		return ml_accts(s);
	if (!strcmp(sub, "open") && ac == 3)
		return ml_open(s, av[2]);
	if (!strcmp(sub, "close") && ac == 3)
		return ml_closecmd(av[2]);
	if (!strcmp(sub, "parse") && ac == 3)
		return ml_parsecmd(s, av[2]);
	if (!strcmp(sub, "part") && ac == 5)
		return ml_partcmd(av[2], av[3], av[4]);
	if (!strcmp(sub, "build") && ac >= 3)
		return ml_build(s, ac, av);
	if (!strcmp(sub, "send") && ac >= 4) {
		ml_acct *a = ml_acctget(s, av[2]);
		str raw;
		int rc;

		if (!a)
			return HIBR_FAIL;
		s_init(&raw);
		if (ml_slurp(av[3], &raw) != HIBR_OK) {
			s_free(&raw);
			return HIBR_FAIL;
		}
		rc = sm_send(s, a, raw.p, raw.n, ac - 4, av + 4);
		s_free(&raw);
		return rc;
	}
	if (!strcmp(sub, "clip") && ac == 4) {
		size_t lim = (size_t)strtoul(av[2], 0, 10), n = strlen(av[3]);
		str o;

		while (n > lim)
			n--;
		while (n && n < strlen(av[3]) && ((unsigned char)av[3][n] & 0xC0) == 0x80)
			n--;
		s_init(&o);
		s_add(&o, av[3], n);
		{
			char *q;

			for (q = o.p ? o.p : ""; *q; q++)
				if (*q == '\t' || *q == '\n' || *q == '\r')
					*q = ' ';
		}
		ml_say(s, o.p ? o.p : "");
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "hdec") && ac == 3) {
		str o;

		s_init(&o);
		ml_hdec(&o, av[2], strlen(av[2]));
		ml_say(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "list") && ac == 3) {
		if (!(x = ml_sess_get(av[2], ML_POP)))
			return HIBR_FAIL;
		return po_list(s, x->pop);
	}
	if (!strcmp(sub, "delete") && ac == 4) {
		if (!(x = ml_sess_get(av[2], ML_POP)))
			return HIBR_FAIL;
		return po_dele(x->pop, av[3]);
	}
	if (!strcmp(sub, "fetch") && ac == 5) {
		if (!(x = ml_sess_get(av[2], -1)))
			return HIBR_FAIL;
		return ml_fetch(s, x, av[3], av[4]);
	}
	if (ac >= 3 && (x = ml_sess_get(av[2], ML_IMAP))) {
		ml_imap *m = x->im;

		if (!strcmp(sub, "folders"))
			return ml_folders(s, m);
		if (!strcmp(sub, "select") && ac >= 4)
			return ml_select(s, m, av[3], ac > 4 && !strcmp(av[4], "-r"));
		if (!strcmp(sub, "headers") && ac == 4)
			return ml_headers(s, m, av[3], 0);
		if (!strcmp(sub, "flags") && ac == 4)
			return ml_headers(s, m, av[3], 1);
		if (!strcmp(sub, "uids"))
			return ml_uids(s, m);
		if (!strcmp(sub, "store") && ac >= 5)
			return ml_store(m, ac, av, 0);
		if (!strcmp(sub, "label") && ac >= 5)
			return ml_store(m, ac, av, 1);
		if ((!strcmp(sub, "move") || !strcmp(sub, "copy")) && ac == 5)
			return ml_move(m, av[3], av[4], sub[0] == 'm');
		if (!strcmp(sub, "expunge"))
			return im_cmd(m, "EXPUNGE", 0);
		if (!strcmp(sub, "append") && ac >= 5)
			return ml_append(m, ac, av);
		if (!strcmp(sub, "idle"))
			return ml_idle(s, m, ac > 3 ? atoi(av[3]) : 0);
		if (!strcmp(sub, "gmail")) {
			ml_say(s, m->gmail ? "1" : "0");
			return HIBR_OK;
		}
	} else if (ac >= 3 && strcmp(sub, "account")) {
		return HIBR_FAIL;
	}
	lg(HIBR_LERR, "usage: email account set|rm NAME ... | account mv OLD NEW | accounts | open ACCOUNT | close h | "
		      "folders h | select h folder [-r] | headers|flags h uids | uids h | "
		      "fetch h uid file | store|label h uids +x -y | move|copy h uids folder | "
		      "expunge h | append h folder file [flags] | idle h [secs] | list h | "
		      "delete h n | send ACCOUNT file [rcpt...] | parse file | part file i out | "
		      "build out -f from ... | hdec text | clip bytes text");
	return 2;
}

/* Close every session when the module goes. */
void ml_fini(sh *s)
{
	size_t i;

	(void)s;
	for (i = 0; i < ml_sessions.n; i++) {
		ml_sess *x = ml_sessions.p[i];

		if (!x)
			continue;
		if (x->im)
			im_close(x->im);
		if (x->pop)
			po_close(x->pop);
		free(x);
	}
	v_free(&ml_sessions);
	ml_conffree();
}

const hibr_bi mail_bi[] = {
	{ "email", m_mail, "email: IMAP with Gmail's labels and IDLE, POP3, SMTP, MIME" },
	HIBR_BI_END
};

HIBR_MODULE("email", HIBR_VER, "email: IMAP, POP3, SMTP and MIME, for reading offline",
	    mail_bi, 0, ml_fini);
