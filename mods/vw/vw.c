#define _GNU_SOURCE

#include "hibr.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef VW_KEYLEN
#define VW_KEYLEN 64
#endif

int sx_out(sh *s, const char *nm, const char *v);

typedef void *(*vw_md_f)(void);
typedef int (*vw_pbkdf2_f)(const char *, int, const unsigned char *, int, int, const void *, int, unsigned char *);
typedef unsigned char *(*vw_hmac_f)(const void *, const void *, int, const unsigned char *, size_t, unsigned char *, unsigned int *);
typedef void *(*vw_ctxnew_f)(void);
typedef void (*vw_ctxfree_f)(void *);
typedef int (*vw_init_f)(void *, const void *, void *, const unsigned char *, const unsigned char *);
typedef int (*vw_upd_f)(void *, unsigned char *, int *, const unsigned char *, int);
typedef int (*vw_fin_f)(void *, unsigned char *, int *);
typedef int (*vw_rand_f)(unsigned char *, int);
typedef int (*vw_memcmp_f)(const void *, const void *, size_t);
typedef int (*vw_digest_f)(const void *, size_t, unsigned char *, unsigned int *, const void *, void *);
typedef int (*vw_argon_f)(uint32_t, uint32_t, uint32_t, const void *, size_t, const void *, size_t, void *, size_t);

static void *vw_lib, *vw_alib;
static vw_md_f vw_sha256, vw_sha1, vw_aes;
static vw_pbkdf2_f vw_pbkdf2;
static vw_hmac_f vw_hmacf;
static vw_ctxnew_f vw_ctxnew;
static vw_ctxfree_f vw_ctxfree;
static vw_init_f vw_decinit, vw_encinit;
static vw_upd_f vw_decupd, vw_encupd;
static vw_fin_f vw_decfin, vw_encfin;
static vw_rand_f vw_rand;
static vw_memcmp_f vw_cmp;
static vw_digest_f vw_digest;
static vw_argon_f vw_argon;

static unsigned char vw_mk[32], vw_uk[VW_KEYLEN];
static int vw_hasmk, vw_hasuk;
static time_t vw_used, vw_idle;

/* Zero memory so the compiler keeps the store. */
void vw_wipe(void *p, size_t n)
{
	volatile unsigned char *q = p;

	while (n--)
		*q++ = 0;
}

/* Bind one symbol from libcrypto or libargon2. */
void *vw_sym(void *lib, const char *nm)
{
	void *f = dlsym(lib, nm);

	if (!f)
		lg(HIBR_LERR, "vwk: the crypto library has no %s", nm);
	return f;
}

/* Load libcrypto on first use, as TLS loads libssl; 0 when it is there. */
int vw_load(void)
{
	const char *names[] = {
#ifdef __APPLE__
		"/opt/homebrew/opt/openssl@3/lib/libcrypto.3.dylib",
		"/usr/local/opt/openssl@3/lib/libcrypto.3.dylib",
#endif
		"libcrypto.so.3", "libcrypto.so.1.1", "libcrypto.so", 0
	};
	int i;

	if (vw_lib)
		return 0;
	for (i = 0; names[i] && !vw_lib; i++)
		vw_lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
	if (!vw_lib) {
		lg(HIBR_LERR, "vwk: cannot load libcrypto: %s", dlerror());
		return -1;
	}
	vw_sha256 = (vw_md_f)vw_sym(vw_lib, "EVP_sha256");
	vw_sha1 = (vw_md_f)vw_sym(vw_lib, "EVP_sha1");
	vw_aes = (vw_md_f)vw_sym(vw_lib, "EVP_aes_256_cbc");
	vw_pbkdf2 = (vw_pbkdf2_f)vw_sym(vw_lib, "PKCS5_PBKDF2_HMAC");
	vw_hmacf = (vw_hmac_f)vw_sym(vw_lib, "HMAC");
	vw_ctxnew = (vw_ctxnew_f)vw_sym(vw_lib, "EVP_CIPHER_CTX_new");
	vw_ctxfree = (vw_ctxfree_f)vw_sym(vw_lib, "EVP_CIPHER_CTX_free");
	vw_decinit = (vw_init_f)vw_sym(vw_lib, "EVP_DecryptInit_ex");
	vw_decupd = (vw_upd_f)vw_sym(vw_lib, "EVP_DecryptUpdate");
	vw_decfin = (vw_fin_f)vw_sym(vw_lib, "EVP_DecryptFinal_ex");
	vw_encinit = (vw_init_f)vw_sym(vw_lib, "EVP_EncryptInit_ex");
	vw_encupd = (vw_upd_f)vw_sym(vw_lib, "EVP_EncryptUpdate");
	vw_encfin = (vw_fin_f)vw_sym(vw_lib, "EVP_EncryptFinal_ex");
	vw_rand = (vw_rand_f)vw_sym(vw_lib, "RAND_bytes");
	vw_cmp = (vw_memcmp_f)vw_sym(vw_lib, "CRYPTO_memcmp");
	vw_digest = (vw_digest_f)vw_sym(vw_lib, "EVP_Digest");
	if (!vw_sha256 || !vw_sha1 || !vw_aes || !vw_pbkdf2 || !vw_hmacf || !vw_ctxnew || !vw_ctxfree ||
	    !vw_decinit || !vw_decupd || !vw_decfin || !vw_encinit || !vw_encupd || !vw_encfin ||
	    !vw_rand || !vw_cmp || !vw_digest) {
		dlclose(vw_lib);
		vw_lib = 0;
		return -1;
	}
	return 0;
}

/* Load libargon2 when an account asks for Argon2id; 0 when it is there. */
int vw_aload(void)
{
	const char *names[] = {
#ifdef __APPLE__
		"/opt/homebrew/opt/argon2/lib/libargon2.1.dylib", "/usr/local/opt/argon2/lib/libargon2.1.dylib",
#endif
		"libargon2.so.1", "libargon2.so", 0
	};
	int i;

	if (vw_alib)
		return 0;
	for (i = 0; names[i] && !vw_alib; i++)
		vw_alib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
	if (!vw_alib || !(vw_argon = (vw_argon_f)vw_sym(vw_alib, "argon2id_hash_raw"))) {
		lg(HIBR_LERR, "vwk: this account uses Argon2id, and libargon2 cannot be loaded");
		return -1;
	}
	return 0;
}

static const char vw_b64c[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Bytes as base64. */
void vw_b64e(str *o, const unsigned char *p, size_t n)
{
	size_t i;
	unsigned v;

	for (i = 0; i + 2 < n; i += 3) {
		v = p[i] << 16 | p[i + 1] << 8 | p[i + 2];
		s_ch(o, vw_b64c[v >> 18 & 63]);
		s_ch(o, vw_b64c[v >> 12 & 63]);
		s_ch(o, vw_b64c[v >> 6 & 63]);
		s_ch(o, vw_b64c[v & 63]);
	}
	if (n - i == 1) {
		v = p[i] << 16;
		s_ch(o, vw_b64c[v >> 18 & 63]);
		s_ch(o, vw_b64c[v >> 12 & 63]);
		s_cat(o, "==");
	} else if (n - i == 2) {
		v = p[i] << 16 | p[i + 1] << 8;
		s_ch(o, vw_b64c[v >> 18 & 63]);
		s_ch(o, vw_b64c[v >> 12 & 63]);
		s_ch(o, vw_b64c[v >> 6 & 63]);
		s_ch(o, '=');
	}
}

/* Base64 (n bytes of it) into bytes; -1 on anything that is not base64. */
long vw_b64d(str *o, const char *p, size_t n)
{
	unsigned v = 0;
	int bits = 0;
	const char *c;
	size_t i;
	long out = 0;

	for (i = 0; i < n; i++) {
		if (p[i] == '=')
			break;
		if (p[i] == '-' || p[i] == '_')
			c = vw_b64c + (p[i] == '-' ? 62 : 63);
		else if (!p[i] || !(c = strchr(vw_b64c, p[i])))
			return -1;
		v = v << 6 | (unsigned)(c - vw_b64c);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			s_ch(o, (char)(v >> bits & 255));
			out++;
		}
	}
	return out;
}

/* The two halves HKDF-Expand makes of a 32-byte key: an encryption key and a MAC key. */
void vw_stretch(const unsigned char *k, unsigned char *out)
{
	unsigned char b[33];
	unsigned int n = 32;

	memcpy(b, "enc\001", 4);
	vw_hmacf(vw_sha256(), k, 32, b, 4, out, &n);
	memcpy(b, "mac\001", 4);
	n = 32;
	vw_hmacf(vw_sha256(), k, 32, b, 4, out + 32, &n);
	vw_wipe(b, sizeof b);
}

/* The master key from a password, as the account's KDF says; 0 on success. */
int vw_derive(const char *pw, const char *email, int argon, long iter, long mem, long par, unsigned char *out)
{
	str e;
	size_t i;
	unsigned char salt[32];
	unsigned int sn = 32;
	int rc;

	s_init(&e);
	while (*email == ' ' || *email == '\t')
		email++;
	for (i = 0; email[i]; i++)
		s_ch(&e, email[i] >= 'A' && email[i] <= 'Z' ? email[i] + 32 : email[i]);
	while (e.n && (e.p[e.n - 1] == ' ' || e.p[e.n - 1] == '\t'))
		e.n--;
	s_grow(&e, 1);
	e.p[e.n] = 0;
	if (!argon) {
		rc = vw_pbkdf2(pw, (int)strlen(pw), (unsigned char *)e.p, (int)e.n, (int)iter, vw_sha256(), 32, out) == 1 ? 0 : -1;
	} else {
		if (vw_aload() < 0) {
			s_free(&e);
			return -1;
		}
		vw_digest(e.p, e.n, salt, &sn, vw_sha256(), 0);
		rc = vw_argon((uint32_t)iter, (uint32_t)(mem * 1024), (uint32_t)par, pw, strlen(pw), salt, 32, out, 32) == 0 ? 0 : -1;
		vw_wipe(salt, sizeof salt);
	}
	vw_wipe(e.p, e.n);
	s_free(&e);
	return rc;
}

/* Decrypt a type-2 EncString "2.iv|ct|mac" with a 64-byte key into o; 0 when the MAC holds. */
int vw_decs(const char *es, const unsigned char *key, str *o)
{
	str iv, ct, mac, buf;
	const char *p = es, *a, *b;
	unsigned char m[32];
	unsigned int mn = 32;
	void *ctx;
	int n1 = 0, n2 = 0, rc = -1;

	if (strncmp(p, "2.", 2))
		return -1;
	p += 2;
	if (!(a = strchr(p, '|')) || !(b = strchr(a + 1, '|')))
		return -1;
	s_init(&iv);
	s_init(&ct);
	s_init(&mac);
	s_init(&buf);
	if (vw_b64d(&iv, p, a - p) != 16 || vw_b64d(&ct, a + 1, b - a - 1) < 16 || vw_b64d(&mac, b + 1, strlen(b + 1)) != 32)
		goto out;
	s_add(&buf, iv.p, iv.n);
	s_add(&buf, ct.p, ct.n);
	vw_hmacf(vw_sha256(), key + 32, 32, (unsigned char *)buf.p, buf.n, m, &mn);
	if (vw_cmp(m, mac.p, 32))
		goto out;
	ctx = vw_ctxnew();
	s_grow(o, ct.n + 32);
	if (vw_decinit(ctx, vw_aes(), 0, key, (unsigned char *)iv.p) == 1 &&
	    vw_decupd(ctx, (unsigned char *)o->p + o->n, &n1, (unsigned char *)ct.p, (int)ct.n) == 1 &&
	    vw_decfin(ctx, (unsigned char *)o->p + o->n + n1, &n2) == 1) {
		o->n += n1 + n2;
		rc = 0;
	}
	vw_ctxfree(ctx);
out:
	vw_wipe(buf.p, buf.n);
	s_free(&iv);
	s_free(&ct);
	s_free(&mac);
	s_free(&buf);
	return rc;
}

/* Encrypt n bytes with a 64-byte key as a type-2 EncString into o; 0 on success. */
int vw_encs(const unsigned char *p, size_t n, const unsigned char *key, str *o)
{
	unsigned char iv[16], m[32];
	unsigned int mn = 32;
	str ct, buf;
	void *ctx;
	int n1 = 0, n2 = 0, rc = -1;

	if (vw_rand(iv, 16) != 1)
		return -1;
	s_init(&ct);
	s_init(&buf);
	s_grow(&ct, n + 32);
	ctx = vw_ctxnew();
	if (vw_encinit(ctx, vw_aes(), 0, key, iv) == 1 &&
	    vw_encupd(ctx, (unsigned char *)ct.p, &n1, p, (int)n) == 1 &&
	    vw_encfin(ctx, (unsigned char *)ct.p + n1, &n2) == 1) {
		ct.n = n1 + n2;
		s_add(&buf, (char *)iv, 16);
		s_add(&buf, ct.p, ct.n);
		vw_hmacf(vw_sha256(), key + 32, 32, (unsigned char *)buf.p, buf.n, m, &mn);
		s_cat(o, "2.");
		vw_b64e(o, iv, 16);
		s_ch(o, '|');
		vw_b64e(o, (unsigned char *)ct.p, ct.n);
		s_ch(o, '|');
		vw_b64e(o, m, 32);
		rc = 0;
	}
	vw_ctxfree(ctx);
	s_free(&ct);
	s_free(&buf);
	return rc;
}

/* Read a secret line: from the terminal with echo off, or from standard input with -s. */
int vw_secret(str *o, int fromin, const char *prompt)
{
	struct termios t, t2;
	int fd = fromin ? 0 : open("/dev/tty", O_RDWR), echo = 0;
	char c;

	if (fd < 0) {
		lg(HIBR_LERR, "vwk: no terminal to ask on");
		return -1;
	}
	if (!fromin) {
		if (write(fd, prompt, strlen(prompt)) < 0)
			echo = 0;
		if (tcgetattr(fd, &t) == 0) {
			t2 = t;
			t2.c_lflag &= ~(tcflag_t)ECHO;
			tcsetattr(fd, TCSAFLUSH, &t2);
			echo = 1;
		}
	}
	while (read(fd, &c, 1) == 1 && c != '\n' && c != '\r')
		s_ch(o, c);
	if (!fromin) {
		if (echo)
			tcsetattr(fd, TCSAFLUSH, &t);
		if (write(fd, "\n", 1) < 0)
			echo = 0;
		close(fd);
	}
	s_grow(o, 1);
	o->p[o->n] = 0;
	return 0;
}

/* Forget every key held. */
void vw_lockall(void)
{
	vw_wipe(vw_mk, sizeof vw_mk);
	vw_wipe(vw_uk, sizeof vw_uk);
	vw_hasmk = vw_hasuk = 0;
}

/* Whether the vault is open, locking it first if it has idled too long. */
int vw_open(void)
{
	time_t now = time(0);

	if (vw_hasuk && vw_idle > 0 && now - vw_used > vw_idle) {
		lg(HIBR_LINF, "vwk: locked after %ld seconds idle", (long)vw_idle);
		vw_lockall();
	}
	if (vw_hasuk)
		vw_used = now;
	return vw_hasuk;
}

/* A TOTP code for a base32 secret or an otpauth:// address, at a time. */
int vw_totp(const char *in, time_t when, str *o)
{
	str key;
	const char *p = in, *q;
	unsigned v = 0;
	int bits = 0, digits = 6, sha1 = 1, i;
	long period = 30;
	unsigned char msg[8], md[64];
	unsigned int mn = 64, off;
	unsigned long code, ctr, mod = 1;

	s_init(&key);
	if (!strncmp(p, "otpauth://", 10)) {
		if ((q = strstr(p, "digits=")))
			digits = atoi(q + 7);
		if ((q = strstr(p, "period=")))
			period = atol(q + 7);
		if (strstr(p, "algorithm=SHA256") || strstr(p, "algorithm=SHA512"))
			sha1 = 0;
		if (!(p = strstr(p, "secret="))) {
			s_free(&key);
			return -1;
		}
		p += 7;
	}
	if (!sha1 || digits < 6 || digits > 8 || period < 1) {
		lg(HIBR_LERR, "vwk totp: only SHA-1 codes of 6 to 8 digits are made");
		s_free(&key);
		return -1;
	}
	for (; *p && *p != '&'; p++) {
		char c = *p >= 'a' && *p <= 'z' ? *p - 32 : *p;
		if (c == ' ' || c == '=' || c == '-')
			continue;
		if (c >= 'A' && c <= 'Z')
			v = v << 5 | (unsigned)(c - 'A');
		else if (c >= '2' && c <= '7')
			v = v << 5 | (unsigned)(c - '2' + 26);
		else {
			s_free(&key);
			return -1;
		}
		bits += 5;
		if (bits >= 8) {
			bits -= 8;
			s_ch(&key, (char)(v >> bits & 255));
		}
	}
	ctr = (unsigned long)(when / period);
	for (i = 7; i >= 0; i--) {
		msg[i] = (unsigned char)(ctr & 255);
		ctr >>= 8;
	}
	vw_hmacf(vw_sha1(), key.p, (int)key.n, msg, 8, md, &mn);
	off = md[19] & 15;
	code = ((unsigned long)(md[off] & 127) << 24) | (unsigned long)md[off + 1] << 16 |
	       (unsigned long)md[off + 2] << 8 | md[off + 3];
	for (i = 0; i < digits; i++)
		mod *= 10;
	code %= mod;
	s_grow(o, (size_t)digits + 1);
	for (i = digits - 1; i >= 0; i--) {
		o->p[o->n + i] = (char)('0' + code % 10);
		code /= 10;
	}
	o->n += digits;
	vw_wipe(key.p, key.n);
	s_free(&key);
	return 0;
}

/* Say or bind a result, wiping the copy. */
int vw_say(sh *s, str *o)
{
	int rc;

	s_grow(o, 1);
	o->p[o->n] = 0;
	rc = sx_out(s, 0, o->p);
	vw_wipe(o->p, o->n);
	s_free(o);
	return rc;
}

/* Text made safe for a form body: every byte but A-Z a-z 0-9 . _ ~ - as %XX. */
void vw_form(str *o, const char *t)
{
	static const char hx[] = "0123456789ABCDEF";
	const unsigned char *p;

	for (p = (const unsigned char *)t; *p; p++) {
		if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr("._~-", *p)) {
			s_ch(o, (char)*p);
			continue;
		}
		s_ch(o, '%');
		s_ch(o, hx[*p >> 4]);
		s_ch(o, hx[*p & 15]);
	}
}

/* Read -e email -k pbkdf2|argon2 -i iterations -m MiB -p parallelism -s from av at *k. */
int vw_kdfargs(int ac, char **av, int *k, const char **email, int *argon, long *it, long *mem, long *par, int *fromin)
{
	while (*k < ac && av[*k][0] == '-') {
		const char *o = av[*k];
		if (!strcmp(o, "-s")) {
			*fromin = 1;
			(*k)++;
			continue;
		}
		if (*k + 1 >= ac)
			return -1;
		if (!strcmp(o, "-e"))
			*email = av[*k + 1];
		else if (!strcmp(o, "-k"))
			*argon = !strcmp(av[*k + 1], "argon2");
		else if (!strcmp(o, "-i"))
			*it = atol(av[*k + 1]);
		else if (!strcmp(o, "-m"))
			*mem = atol(av[*k + 1]);
		else if (!strcmp(o, "-p"))
			*par = atol(av[*k + 1]);
		else
			return -1;
		*k += 2;
	}
	return *email && *it > 0 ? 0 : -1;
}

/* vwk: the Bitwarden vault's keys, held in this shell's memory and nowhere else. */
int vw_bi(sh *s, int ac, char **av)
{
	const char *email = 0, *cmd;
	int argon = 0, fromin = 0, k = 2;
	long it = 0, mem = 64, par = 4;
	unsigned char key[VW_KEYLEN], mk[32], st[VW_KEYLEN];
	str o, sec;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: vwk hash|open|dec|enc|session|pin|totp|form|lock|state|idle ...");
		return 2;
	}
	cmd = av[1];
	if (!strcmp(cmd, "form")) {
		if (ac != 3) {
			lg(HIBR_LERR, "usage: vwk form text");
			return 2;
		}
		s_init(&o);
		vw_form(&o, av[2]);
		return vw_say(s, &o);
	}
	if (!strcmp(cmd, "lock")) {
		vw_lockall();
		return HIBR_OK;
	}
	if (!strcmp(cmd, "idle")) {
		vw_idle = ac > 2 ? atol(av[2]) : 0;
		return HIBR_OK;
	}
	if (!strcmp(cmd, "state")) {
		s_init(&o);
		s_cat(&o, vw_open() ? "unlocked" : "locked");
		return vw_say(s, &o);
	}
	if (vw_load() < 0)
		return HIBR_FAIL;
	s_init(&o);
	s_init(&sec);
	if (!strcmp(cmd, "totp")) {
		if (ac < 3 || vw_totp(av[2], ac > 3 ? (time_t)atol(av[3]) : time(0), &o) < 0) {
			lg(HIBR_LERR, "vwk totp: not a TOTP secret");
			s_free(&o);
			return HIBR_FAIL;
		}
		return vw_say(s, &o);
	}
	if (!strcmp(cmd, "hash")) {
		if (vw_kdfargs(ac, av, &k, &email, &argon, &it, &mem, &par, &fromin) < 0) {
			lg(HIBR_LERR, "usage: vwk hash -e email -k pbkdf2|argon2 -i iterations [-m MiB -p parallelism] [-s]");
			return 2;
		}
		vw_secret(&sec, fromin, "Master password: ");
		if (vw_derive(sec.p, email, argon, it, mem, par, vw_mk) < 0) {
			vw_wipe(sec.p, sec.n);
			s_free(&sec);
			return HIBR_FAIL;
		}
		vw_hasmk = 1;
		if (vw_pbkdf2((char *)vw_mk, 32, (unsigned char *)sec.p, (int)sec.n, 1, vw_sha256(), 32, mk) != 1) {
			vw_wipe(sec.p, sec.n);
			s_free(&sec);
			return HIBR_FAIL;
		}
		vw_wipe(sec.p, sec.n);
		s_free(&sec);
		vw_b64e(&o, mk, 32);
		vw_wipe(mk, sizeof mk);
		return vw_say(s, &o);
	}
	if (!strcmp(cmd, "open")) {
		if (ac != 3 || !vw_hasmk) {
			lg(HIBR_LERR, "vwk open: hash the master password first, then give the account's key");
			return 2;
		}
		vw_stretch(vw_mk, st);
		vw_wipe(vw_mk, sizeof vw_mk);
		vw_hasmk = 0;
		if (vw_decs(av[2], st, &o) < 0 || o.n != VW_KEYLEN) {
			vw_wipe(st, sizeof st);
			vw_wipe(o.p, o.n);
			s_free(&o);
			lg(HIBR_LERR, "vwk open: wrong master password, or not this account's key");
			return HIBR_FAIL;
		}
		memcpy(vw_uk, o.p, VW_KEYLEN);
		vw_wipe(st, sizeof st);
		vw_wipe(o.p, o.n);
		s_free(&o);
		vw_hasuk = 1;
		vw_used = time(0);
		return HIBR_OK;
	}
	if (!strcmp(cmd, "dec") || !strcmp(cmd, "enc")) {
		const char *ik = 0;
		if (!vw_open()) {
			lg(HIBR_LERR, "vwk %s: the vault is locked", cmd);
			return HIBR_FAIL;
		}
		if (ac > 4 && !strcmp(av[2], "-k")) {
			ik = av[3];
			k = 4;
		}
		if (ac != k + 1) {
			lg(HIBR_LERR, "usage: vwk %s [-k itemkey] text", cmd);
			return 2;
		}
		memcpy(key, vw_uk, VW_KEYLEN);
		if (ik) {
			if (vw_decs(ik, vw_uk, &sec) < 0 || sec.n != VW_KEYLEN) {
				lg(HIBR_LERR, "vwk %s: the item's own key does not open", cmd);
				vw_wipe(key, sizeof key);
				s_free(&sec);
				return HIBR_FAIL;
			}
			memcpy(key, sec.p, VW_KEYLEN);
			vw_wipe(sec.p, sec.n);
			s_free(&sec);
		}
		if (!strcmp(cmd, "dec") ? vw_decs(av[k], key, &o) < 0 : vw_encs((unsigned char *)av[k], strlen(av[k]), key, &o) < 0) {
			vw_wipe(key, sizeof key);
			s_free(&o);
			lg(HIBR_LERR, "vwk %s: does not open with this vault's key", cmd);
			return HIBR_FAIL;
		}
		vw_wipe(key, sizeof key);
		return vw_say(s, &o);
	}
	if (!strcmp(cmd, "session")) {
		if (ac == 3 && !strcmp(av[2], "new")) {
			if (!vw_open()) {
				lg(HIBR_LERR, "vwk session: the vault is locked");
				return HIBR_FAIL;
			}
			if (vw_rand(key, VW_KEYLEN) != 1)
				return HIBR_FAIL;
			vw_b64e(&o, key, VW_KEYLEN);
			s_ch(&o, '\t');
			vw_encs(vw_uk, VW_KEYLEN, key, &o);
			vw_wipe(key, sizeof key);
			return vw_say(s, &o);
		}
		if (ac == 5 && !strcmp(av[2], "open")) {
			if (vw_b64d(&sec, av[3], strlen(av[3])) != VW_KEYLEN || vw_decs(av[4], (unsigned char *)sec.p, &o) < 0 ||
			    o.n != VW_KEYLEN) {
				vw_wipe(sec.p, sec.n);
				s_free(&sec);
				vw_wipe(o.p, o.n);
				s_free(&o);
				return HIBR_FAIL;
			}
			memcpy(vw_uk, o.p, VW_KEYLEN);
			vw_hasuk = 1;
			vw_used = time(0);
			vw_wipe(sec.p, sec.n);
			s_free(&sec);
			vw_wipe(o.p, o.n);
			s_free(&o);
			return HIBR_OK;
		}
		lg(HIBR_LERR, "usage: vwk session new | open key wrapped");
		return 2;
	}
	if (!strcmp(cmd, "pin")) {
		int wrap = ac > 2 && !strcmp(av[2], "wrap"), unwrap = ac > 2 && !strcmp(av[2], "open");
		k = 3;
		if ((!wrap && !unwrap) || vw_kdfargs(ac, av, &k, &email, &argon, &it, &mem, &par, &fromin) < 0 ||
		    (unwrap && ac != k + 1) || (wrap && ac != k)) {
			lg(HIBR_LERR, "usage: vwk pin wrap|open -e email -k pbkdf2|argon2 -i iterations [-m MiB -p par] [-s] [wrapped]");
			return 2;
		}
		if (wrap && !vw_open()) {
			lg(HIBR_LERR, "vwk pin: the vault is locked");
			return HIBR_FAIL;
		}
		vw_secret(&sec, fromin, "PIN: ");
		if (vw_derive(sec.p, email, argon, it, mem, par, mk) < 0) {
			vw_wipe(sec.p, sec.n);
			s_free(&sec);
			return HIBR_FAIL;
		}
		vw_wipe(sec.p, sec.n);
		s_free(&sec);
		vw_stretch(mk, st);
		vw_wipe(mk, sizeof mk);
		if (wrap) {
			vw_encs(vw_uk, VW_KEYLEN, st, &o);
			vw_wipe(st, sizeof st);
			return vw_say(s, &o);
		}
		if (vw_decs(av[k], st, &o) < 0 || o.n != VW_KEYLEN) {
			vw_wipe(st, sizeof st);
			vw_wipe(o.p, o.n);
			s_free(&o);
			lg(HIBR_LERR, "vwk pin: wrong PIN");
			return HIBR_FAIL;
		}
		memcpy(vw_uk, o.p, VW_KEYLEN);
		vw_hasuk = 1;
		vw_used = time(0);
		vw_wipe(st, sizeof st);
		vw_wipe(o.p, o.n);
		s_free(&o);
		return HIBR_OK;
	}
	s_free(&o);
	lg(HIBR_LERR, "vwk: %s: no such command", cmd);
	return 2;
}

/* Wipe the keys when the module goes. */
void vw_fini(sh *s)
{
	(void)s;
	vw_lockall();
	if (vw_lib)
		dlclose(vw_lib);
	if (vw_alib)
		dlclose(vw_alib);
	vw_lib = vw_alib = 0;
}

const hibr_bi vw_bis[] = {
	{ "vwk", vw_bi, "a Bitwarden vault's keys, in memory only: vwk hash|open|dec|enc|session|pin|totp|form|lock|state|idle" },
	HIBR_BI_END
};

HIBR_MODULE("vw", "1.0", "Bitwarden and Vaultwarden: the vault's keys and its crypto, libcrypto loaded at run time",
	    vw_bis, 0, vw_fini);
