#include "pm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void pm_textof(pm_comp *c, const char *name, str *o);
int pm_write(const char *out, str *o);
void pm_tline(str *o, const char *name, const char *v);
void pm_parval(str *l, const char *v);

/* Quoted-printable decoded, for a vCard 2.1 value that says so. */
void pm_qp(str *o, const char *p)
{
	int h, l;

	for (; *p; p++) {
		if (*p == '=' && p[1] && p[2]) {
			h = p[1] >= 'A' ? (p[1] & ~32) - 'A' + 10 : p[1] - '0';
			l = p[2] >= 'A' ? (p[2] & ~32) - 'A' + 10 : p[2] - '0';
			if (h >= 0 && h < 16 && l >= 0 && l < 16) {
				s_ch(o, (char)(h * 16 + l));
				p += 2;
				continue;
			}
		}
		s_ch(o, *p);
	}
}

/* A vCard property's value as text: quoted-printable decoded when it says
   so, escapes taken out. */
void pm_cardval(pm_prop *p, str *o)
{
	const char *enc = pm_pget(p, "ENCODING");

	o->n = 0;
	if (o->p)
		o->p[0] = 0;
	if (enc && !strcasecmp(enc, "QUOTED-PRINTABLE")) {
		str q;

		s_init(&q);
		pm_qp(&q, p->val);
		pm_untext(o, q.p ? q.p : "");
		s_free(&q);
	} else {
		pm_untext(o, p->val);
	}
}

/* The TYPE of a property, every one of them -- TYPE=work,voice and
   TYPE=WORK;TYPE=VOICE alike -- lower case, comma-joined. */
void pm_types(pm_prop *p, str *o)
{
	size_t i;
	const char *v;

	o->n = 0;
	if (o->p)
		o->p[0] = 0;
	for (i = 0; i < p->pars.n; i++) {
		pm_par *a = p->pars.p[i];

		if (strcmp(a->name, "TYPE"))
			continue;
		for (v = a->val; *v; v++) {
			if (*v == '"')
				continue;
			if (o->n && *v != ',' && o->p[o->n - 1] == ',' && 0)
				continue;
			s_ch(o, (char)(*v >= 'A' && *v <= 'Z' ? *v + 32 : *v));
		}
		s_ch(o, ',');
	}
	if (o->n && o->p[o->n - 1] == ',')
		o->n--;
	if (o->p)
		o->p[o->n] = 0;
}

/* One field of a structured value (N, ADR): the n-th, ;-separated, with
   escapes taken out. */
void pm_field(const char *v, int n, str *o)
{
	int i = 0;

	o->n = 0;
	for (; *v; v++) {
		if (*v == '\\' && v[1]) {
			if (i == n)
				s_ch(o, v[1] == 'n' || v[1] == 'N' ? '\n' : v[1]);
			v++;
			continue;
		}
		if (*v == ';') {
			i++;
			continue;
		}
		if (i == n)
			s_ch(o, *v);
	}
	if (o->p)
		o->p[o->n] = 0;
	else
		s_cat(o, "");
}

/* pim vcf cards [-t TEXT | FILE]: every contact, a map each -- uid, fn,
   given, family, nick, org, title, bday, note, url, version, and lists of
   email, tel and adr, each item's value and type. */
int pm_cards(sh *s, int ac, char **av)
{
	str in, k, v, t, sub;
	pm_comp *doc, *c;
	char *ks[4];
	size_t i, j, n = 0;
	int next;
	const char *simple[][2] = { { "UID", "uid" }, { "FN", "fn" }, { "NICKNAME", "nick" },
				    { "TITLE", "title" }, { "BDAY", "bday" }, { "NOTE", "note" },
				    { "URL", "url" }, { "VERSION", "version" },
				    { "CATEGORIES", "categories" }, { "REV", "rev" }, { 0, 0 } };

	s_init(&in);
	if (pm_input(s, ac, av, 3, &in, &next) != HIBR_OK) {
		s_free(&in);
		return HIBR_FAIL;
	}
	doc = pm_parse(in.p ? in.p : "", in.n);
	s_init(&k);
	s_init(&v);
	s_init(&t);
	s_init(&sub);
	for (i = 0; i < doc->kids.n; i++) {
		size_t ne = 0, nt = 0, na = 0;
		int q;

		c = doc->kids.p[i];
		if (strcmp(c->name, "VCARD"))
			continue;
		if (!s->bind) {
			pm_prop *fn = pm_get(c, "FN"), *em = pm_get(c, "EMAIL");

			if (fn)
				pm_cardval(fn, &v);
			printf("%s", fn ? (v.p ? v.p : "") : "");
			if (em)
				pm_cardval(em, &v);
			printf("\t%s\n", em ? (v.p ? v.p : "") : "");
			n++;
			continue;
		}
		k.n = 0;
		s_num(&k, (long)n++);
		ks[0] = k.p;
		for (q = 0; simple[q][0]; q++) {
			pm_prop *p = pm_get(c, simple[q][0]);

			if (!p)
				continue;
			pm_cardval(p, &v);
			ks[1] = (char *)simple[q][1];
			pm_set(s, ks, 2, v.p ? v.p : "");
		}
		{
			pm_prop *p = pm_get(c, "N");

			if (p) {
				pm_field(p->val, 0, &v);
				ks[1] = "family";
				pm_set(s, ks, 2, v.p);
				pm_field(p->val, 1, &v);
				ks[1] = "given";
				pm_set(s, ks, 2, v.p);
			}
			p = pm_get(c, "ORG");
			if (p) {
				pm_field(p->val, 0, &v);
				ks[1] = "org";
				pm_set(s, ks, 2, v.p);
			}
			p = pm_get(c, "PHOTO");
			ks[1] = "photo";
			pm_set(s, ks, 2, p ? "1" : "0");
		}
		for (j = 0; j < c->props.n; j++) {
			pm_prop *p = c->props.p[j];
			const char *list = 0;
			size_t *cnt = 0;

			if (!strcmp(p->name, "EMAIL")) {
				list = "email";
				cnt = &ne;
			} else if (!strcmp(p->name, "TEL")) {
				list = "tel";
				cnt = &nt;
			} else if (!strcmp(p->name, "ADR")) {
				list = "adr";
				cnt = &na;
			}
			if (!list)
				continue;
			sub.n = 0;
			s_num(&sub, (long)(*cnt)++);
			ks[1] = (char *)list;
			ks[2] = sub.p;
			if (!strcmp(list, "adr")) {
				str one;
				int f;

				s_init(&one);
				v.n = 0;
				for (f = 2; f <= 6; f++) {
					pm_field(p->val, f, &one);
					if (!one.n)
						continue;
					if (v.n)
						s_cat(&v, ", ");
					s_add(&v, one.p, one.n);
				}
				s_free(&one);
				if (v.p)
					v.p[v.n] = 0;
			} else {
				pm_cardval(p, &v);
				if (!strncasecmp(v.p ? v.p : "", "tel:", 4)) {
					str w;

					s_init(&w);
					s_cat(&w, v.p + 4);
					v.n = 0;
					s_cat(&v, w.p);
					s_free(&w);
				}
			}
			ks[3] = "v";
			pm_set(s, ks, 4, v.p ? v.p : "");
			pm_types(p, &t);
			ks[3] = "type";
			pm_set(s, ks, 4, t.p ? t.p : "");
		}
	}
	if (s->bind && !n)
		hibr_retn(s, 0, 0);
	lg(HIBR_LDBG, "pim: %zu cards", n);
	s_free(&k);
	s_free(&v);
	s_free(&t);
	s_free(&sub);
	pm_free(doc);
	s_free(&in);
	return HIBR_OK;
}

/* A value with ; and , in it escaped for one field of a structured value,
   or the field of a list. */
void pm_sfield(str *o, const char *v)
{
	for (; *v; v++) {
		if (*v == '\\' || *v == ';' || *v == ',')
			s_ch(o, '\\');
		if (*v == '\n') {
			s_cat(o, "\\n");
			continue;
		}
		s_ch(o, *v);
	}
}

/* A typed property, from value[;type,type]. */
void pm_typed(str *o, const char *name, const char *spec, int v4)
{
	char *dup = xs(spec), *ty = strchr(dup, ';');
	str l;

	if (ty)
		*ty++ = 0;
	s_init(&l);
	s_cat(&l, name);
	if (ty && *ty) {
		s_cat(&l, ";TYPE=");
		pm_parval(&l, ty);
	}
	s_ch(&l, ':');
	if (!strcmp(name, "TEL") && v4) {
		s_cat(&l, "tel:");
		s_cat(&l, dup);
	} else {
		pm_sfield(&l, dup);
	}
	pm_lineb(o, &l);
	s_free(&l);
	free(dup);
}

/* pim vcf build OUT [options]: write one contact as vCard 3.0, or 4.0 with
   -V 4.0. -u uid, -f full name, -n "family;given", -e email[;type]
   (repeatable), -t tel[;type] (repeatable), -a "street;city;region;code;
   country[;type]" (repeatable), -o organisation, -T title, -b birthday,
   -N note, -U url, -k nickname, -C categories. */
int pm_cbuild(sh *s, int ac, char **av)
{
	const char *out, *uid = 0, *fn = 0, *n = 0, *org = 0, *title = 0, *bday = 0, *note = 0;
	const char *url = 0, *nick = 0, *cats = 0, *ver = "3.0";
	vec em = { 0, 0, 0 }, tel = { 0, 0, 0 }, adr = { 0, 0, 0 };
	str o, l;
	int i, rc, v4;
	size_t j;

	(void)s;
	if (ac < 4) {
		lg(HIBR_LERR, "usage: pim vcf build out -u uid -f name [-n family;given] [-e email]... "
			      "[-t tel]... [-a adr]... [-o org] [-T title] [-b bday] [-N note] [-U url] "
			      "[-k nick] [-C categories] [-V 3.0|4.0]");
		return 2;
	}
	out = av[3];
	for (i = 4; i + 1 < ac; i += 2) {
		const char *a = av[i], *v = av[i + 1];

		if (a[0] != '-' || !a[1] || a[2])
			break;
		switch (a[1]) {
		case 'u': uid = v; break;
		case 'f': fn = v; break;
		case 'n': n = v; break;
		case 'e': v_add(&em, (void *)v); break;
		case 't': v_add(&tel, (void *)v); break;
		case 'a': v_add(&adr, (void *)v); break;
		case 'o': org = v; break;
		case 'T': title = v; break;
		case 'b': bday = v; break;
		case 'N': note = v; break;
		case 'U': url = v; break;
		case 'k': nick = v; break;
		case 'C': cats = v; break;
		case 'V': ver = v; break;
		default: i = ac; break;
		}
	}
	if (i < ac || !uid || !fn || (strcmp(ver, "3.0") && strcmp(ver, "4.0"))) {
		lg(HIBR_LERR, "pim: vcf build: a contact needs -u uid and -f name, and options in pairs");
		v_free(&em);
		v_free(&tel);
		v_free(&adr);
		return 2;
	}
	v4 = !strcmp(ver, "4.0");
	s_init(&o);
	s_init(&l);
	pm_line(&o, "BEGIN:VCARD");
	l.n = 0;
	s_cat(&l, "VERSION:");
	s_cat(&l, ver);
	pm_lineb(&o, &l);
	pm_line(&o, "PRODID:-//hibr//pim//EN");
	l.n = 0;
	s_cat(&l, "UID:");
	s_cat(&l, uid);
	pm_lineb(&o, &l);
	pm_tline(&o, "FN", fn);
	l.n = 0;
	s_cat(&l, "N:");
	if (n) {
		const char *semi = strchr(n, ';');

		if (semi) {
			char *fam = xm((size_t)(semi - n) + 1);

			memcpy(fam, n, (size_t)(semi - n));
			fam[semi - n] = 0;
			pm_sfield(&l, fam);
			s_ch(&l, ';');
			pm_sfield(&l, semi + 1);
			free(fam);
		} else {
			pm_sfield(&l, n);
			s_ch(&l, ';');
		}
	} else {
		pm_sfield(&l, fn);
		s_ch(&l, ';');
	}
	s_cat(&l, ";;;");
	pm_lineb(&o, &l);
	if (nick)
		pm_tline(&o, "NICKNAME", nick);
	if (org) {
		l.n = 0;
		s_cat(&l, "ORG:");
		pm_sfield(&l, org);
		pm_lineb(&o, &l);
	}
	if (title)
		pm_tline(&o, "TITLE", title);
	for (j = 0; j < em.n; j++)
		pm_typed(&o, "EMAIL", em.p[j], v4);
	for (j = 0; j < tel.n; j++)
		pm_typed(&o, "TEL", tel.p[j], v4);
	for (j = 0; j < adr.n; j++) {
		char *dup = xs(adr.p[j]), *f[6] = { 0, 0, 0, 0, 0, 0 }, *p = dup;
		int k = 0;

		while (k < 6) {
			f[k++] = p;
			p = strchr(p, ';');
			if (!p)
				break;
			*p++ = 0;
		}
		l.n = 0;
		s_cat(&l, "ADR");
		if (f[5] && *f[5]) {
			s_cat(&l, ";TYPE=");
			pm_parval(&l, f[5]);
		}
		s_cat(&l, ":;;");
		for (k = 0; k < 5; k++) {
			if (k)
				s_ch(&l, ';');
			if (f[k])
				pm_sfield(&l, f[k]);
		}
		pm_lineb(&o, &l);
		free(dup);
	}
	if (bday) {
		l.n = 0;
		s_cat(&l, "BDAY:");
		s_cat(&l, bday);
		pm_lineb(&o, &l);
	}
	if (url) {
		l.n = 0;
		s_cat(&l, "URL:");
		s_cat(&l, url);
		pm_lineb(&o, &l);
	}
	if (cats) {
		l.n = 0;
		s_cat(&l, "CATEGORIES:");
		s_cat(&l, cats);
		pm_lineb(&o, &l);
	}
	pm_tline(&o, "NOTE", note);
	pm_line(&o, "END:VCARD");
	rc = pm_write(out, &o);
	s_free(&o);
	s_free(&l);
	v_free(&em);
	v_free(&tel);
	v_free(&adr);
	return rc;
}

/* pim vcf: vCard -- cards, build. */
int pm_vcf(sh *s, int ac, char **av)
{
	const char *sub = ac > 2 ? av[2] : "";

	if (!strcmp(sub, "cards"))
		return pm_cards(s, ac, av);
	if (!strcmp(sub, "build"))
		return pm_cbuild(s, ac, av);
	lg(HIBR_LERR, "usage: pim vcf cards|build ...");
	return 2;
}
