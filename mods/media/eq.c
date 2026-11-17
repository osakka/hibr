#include "mv.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* A band's centre, by its number: a low shelf, eight peaks and a high shelf -- ten because a ten-band graphic equaliser is what this is. */
static const double mv_eqhz[10] = { 60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000 };

/* One band's biquad at a rate, normalised by a0 into c: b0 b1 b2 a1 a2 (RBJ's cookbook). */
void mv_eqcoef(double *c, int band, double db, double rate)
{
	double f = mv_eqhz[band], a = pow(10.0, db / 40.0), w = 2.0 * M_PI * f / rate;
	double cw = cos(w), sw = sin(w), al, sa, b0, b1, b2, a0, a1, a2;

	if (db == 0.0 || f >= rate * 0.45) {
		c[0] = 1.0;
		c[1] = c[2] = c[3] = c[4] = 0.0;
		return;
	}
	if (band == 0 || band == 9) {
		al = sw / 2.0 * sqrt(2.0);
		sa = 2.0 * sqrt(a) * al;
		if (band == 0) {
			b0 = a * ((a + 1) - (a - 1) * cw + sa);
			b1 = 2 * a * ((a - 1) - (a + 1) * cw);
			b2 = a * ((a + 1) - (a - 1) * cw - sa);
			a0 = (a + 1) + (a - 1) * cw + sa;
			a1 = -2 * ((a - 1) + (a + 1) * cw);
			a2 = (a + 1) + (a - 1) * cw - sa;
		} else {
			b0 = a * ((a + 1) + (a - 1) * cw + sa);
			b1 = -2 * a * ((a - 1) + (a + 1) * cw);
			b2 = a * ((a + 1) + (a - 1) * cw - sa);
			a0 = (a + 1) - (a - 1) * cw + sa;
			a1 = 2 * ((a - 1) - (a + 1) * cw);
			a2 = (a + 1) - (a - 1) * cw - sa;
		}
	} else {
		al = sw / 2.0;
		b0 = 1 + al * a;
		b1 = -2 * cw;
		b2 = 1 - al * a;
		a0 = 1 + al / a;
		a1 = -2 * cw;
		a2 = 1 - al / a;
	}
	c[0] = b0 / a0;
	c[1] = b1 / a0;
	c[2] = b2 / a0;
	c[3] = a1 / a0;
	c[4] = a2 / a0;
}

/* The sound thread's own copy of the EQ, the mutex held: coefficients made again when the verb has changed something or the rate is new; whether it is on. */
int mv_eqsync(mv_pl *p, int ch)
{
	int b;

	if (!p->eqon)
		return 0;
	if (!p->eqz || p->eqch != ch) {
		free(p->eqz);
		p->eqz = xm((size_t)ch * 10 * 4 * sizeof *p->eqz);
		memset(p->eqz, 0, (size_t)ch * 10 * 4 * sizeof *p->eqz);
		p->eqch = ch;
		p->eqdone = p->eqgen - 1;
	}
	if (p->eqdone != p->eqgen || p->eqrate != p->ao.rate) {
		for (b = 0; b < 10; b++)
			mv_eqcoef(p->eqc[b], b, p->eqdb[b], (double)p->ao.rate);
		p->eqmul = pow(10.0, p->eqpre / 20.0);
		p->eqdone = p->eqgen;
		p->eqrate = p->ao.rate;
	}
	return 1;
}

/* A block through the ten bands and the preamp, each channel with its own history, in the sound thread; clipped to sixteen bits. */
void mv_eqrun(mv_pl *p, int16_t *s, int n, int ch)
{
	int i, c, b;
	double x, y, *z, *k;

	for (i = 0; i < n; i++)
		for (c = 0; c < ch; c++) {
			x = s[i * ch + c];
			for (b = 0; b < 10; b++) {
				k = p->eqc[b];
				z = p->eqz + ((size_t)c * 10 + b) * 4;
				y = k[0] * x + k[1] * z[0] + k[2] * z[1] - k[3] * z[2] - k[4] * z[3];
				z[1] = z[0];
				z[0] = x;
				z[3] = z[2];
				z[2] = y;
				x = y;
			}
			x *= p->eqmul;
			s[i * ch + c] = x > 32767 ? 32767 : x < -32768 ? -32768 : (int16_t)x;
		}
}

/* A block just written to the device, kept in the ring as stereo frames for spectrum and wave, the mutex held: a mono block on both sides. */
void mv_ringpush(mv_pl *p, const int16_t *s, int n, int ch)
{
	int i;

	if (!p->ring) {
		p->ring = xm((size_t)MV_RINGN * 2 * sizeof *p->ring);
		memset(p->ring, 0, (size_t)MV_RINGN * 2 * sizeof *p->ring);
	}
	for (i = 0; i < n; i++) {
		p->ring[p->ringw * 2] = s[i * ch];
		p->ring[p->ringw * 2 + 1] = s[i * ch + (ch > 1)];
		p->ringw = (p->ringw + 1) % MV_RINGN;
	}
	p->ringn += n;
	if (p->ringn > MV_RINGN)
		p->ringn = MV_RINGN;
}

/* The newest n stereo frames, oldest first, into l and r as -1 to 1; how many there were. */
int mv_ringcopy(mv_pl *p, double *l, double *r, int n)
{
	int i, k, have;

	pthread_mutex_lock(&p->mu);
	have = p->ringn < n ? p->ringn : n;
	for (i = 0; i < n; i++)
		l[i] = r[i] = 0.0;
	for (i = 0; i < have; i++) {
		k = (p->ringw - have + i + MV_RINGN) % MV_RINGN;
		l[n - have + i] = p->ring[k * 2] / 32768.0;
		r[n - have + i] = p->ring[k * 2 + 1] / 32768.0;
	}
	pthread_mutex_unlock(&p->mu);
	return have;
}

/* An in-place radix-2 FFT of n (a power of two) complex values. */
void mv_fft(double *re, double *im, int n)
{
	int i, j, k, m;
	double t, u, wr, wi, cr, ci, a;

	for (i = 1, j = 0; i < n; i++) {
		for (k = n >> 1; j & k; k >>= 1)
			j ^= k;
		j ^= k;
		if (i < j) {
			t = re[i]; re[i] = re[j]; re[j] = t;
			t = im[i]; im[i] = im[j]; im[j] = t;
		}
	}
	for (m = 2; m <= n; m <<= 1) {
		a = -2.0 * M_PI / m;
		wr = cos(a);
		wi = sin(a);
		for (i = 0; i < n; i += m) {
			cr = 1.0;
			ci = 0.0;
			for (j = 0; j < m / 2; j++) {
				t = cr * re[i + j + m / 2] - ci * im[i + j + m / 2];
				u = cr * im[i + j + m / 2] + ci * re[i + j + m / 2];
				re[i + j + m / 2] = re[i + j] - t;
				im[i + j + m / 2] = im[i + j] - u;
				re[i + j] += t;
				im[i + j] += u;
				t = cr * wr - ci * wi;
				ci = cr * wi + ci * wr;
				cr = t;
			}
		}
	}
}

double *mv_hann;

/* The Hann window for a spectrum, made the first time one is asked for and kept: a cosine per point on every call was most of what a call cost. */
const double *mv_window(int n)
{
	int i;

	if (!mv_hann) {
		mv_hann = xm((size_t)n * sizeof *mv_hann);
		for (i = 0; i < n; i++)
			mv_hann[i] = 0.5 - 0.5 * cos(2.0 * M_PI * i / (n - 1));
	}
	return mv_hann;
}

/* The sound thread's spectrum of the newest sound, at most every MV_SPECMS: the squared magnitude of each bin, kept under mu for the verb -- the transform is the sound thread's work, so asking for bars costs the caller nothing but a copy. */
void mv_specupdate(mv_pl *p)
{
	int n = MV_FFTN, i;
	const double *hw = mv_window(MV_FFTN);
	double *l, *r, *re, *im, now = mv_now();

	if (now - p->specat < MV_SPECMS / 1000.0)
		return;
	p->specat = now;
	l = xm((size_t)n * sizeof *l);
	r = xm((size_t)n * sizeof *r);
	re = xm((size_t)n * sizeof *re);
	im = xm((size_t)n * sizeof *im);
	mv_ringcopy(p, l, r, n);
	for (i = 0; i < n; i++) {
		re[i] = (l[i] + r[i]) * 0.5 * hw[i];
		im[i] = 0.0;
	}
	mv_fft(re, im, n);
	pthread_mutex_lock(&p->mu);
	if (!p->spec)
		p->spec = xm((size_t)(n / 2) * sizeof *p->spec);
	for (i = 0; i < n / 2; i++)
		p->spec[i] = re[i] * re[i] + im[i] * im[i];
	pthread_mutex_unlock(&p->mu);
	free(l);
	free(r);
	free(re);
	free(im);
}

/* The newest spectrum as bands bars, log-spaced from 40 Hz to 16 kHz, each its strongest bin from -70 to 0 dBFS onto 0-255, into o as one line: all zeros before the sound has played. */
void mv_spectrum(mv_pl *p, int bands, str *o)
{
	int n = MV_FFTN, k, lo, hi, b, v;
	double rate = p->ao.rate > 0 ? p->ao.rate : 48000, top, e0, e1, m, db;

	top = rate / 2 < 16000 ? rate / 2 : 16000;
	pthread_mutex_lock(&p->mu);
	for (b = 0; b < bands; b++) {
		e0 = 40.0 * pow(top / 40.0, (double)b / bands);
		e1 = 40.0 * pow(top / 40.0, (double)(b + 1) / bands);
		lo = (int)(e0 * n / rate);
		hi = (int)(e1 * n / rate);
		if (lo < 1)
			lo = 1;
		if (hi <= lo)
			hi = lo + 1;
		m = 0.0;
		for (k = lo; p->spec && k < hi && k < n / 2; k++)
			if (p->spec[k] > m)
				m = p->spec[k];
		db = m > 0.0 ? 10.0 * log10(m / ((n / 4.0) * (n / 4.0))) : -100.0;
		v = (int)((db + 70.0) * 255.0 / 70.0);
		if (b)
			s_ch(o, ' ');
		s_num(o, v < 0 ? 0 : v > 255 ? 255 : v);
	}
	pthread_mutex_unlock(&p->mu);
}

/* The newest sound's levels and its shape: PEAKL PEAKR RMSL RMSR (0-255), then on a line of their own points samples of the mono mix, -128 to 127. */
void mv_wave(mv_pl *p, int points, str *o)
{
	int n = MV_FFTN, i, j, v;
	double *l = xm((size_t)n * sizeof *l), *r = xm((size_t)n * sizeof *r);
	double pl = 0, pr = 0, sl = 0, sr = 0, x;

	mv_ringcopy(p, l, r, n);
	for (i = 0; i < n; i++) {
		if (fabs(l[i]) > pl)
			pl = fabs(l[i]);
		if (fabs(r[i]) > pr)
			pr = fabs(r[i]);
		sl += l[i] * l[i];
		sr += r[i] * r[i];
	}
	s_num(o, (int)(pl * 255.0 + 0.5));
	s_ch(o, ' ');
	s_num(o, (int)(pr * 255.0 + 0.5));
	s_ch(o, ' ');
	s_num(o, (int)(sqrt(sl / n) * 255.0 + 0.5));
	s_ch(o, ' ');
	s_num(o, (int)(sqrt(sr / n) * 255.0 + 0.5));
	s_ch(o, '\n');
	for (j = 0; j < points; j++) {
		i = (int)((long)j * n / points);
		x = (l[i] + r[i]) * 0.5;
		v = (int)(x * 127.0);
		if (j)
			s_ch(o, ' ');
		s_num(o, v < -128 ? -128 : v > 127 ? 127 : v);
	}
	free(l);
	free(r);
}
