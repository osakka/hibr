#define _GNU_SOURCE

#include "tm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern vec tm_list;

const py_api *tm_pty;
const dp_api *tm_dp;

/* Look a terminal up by id, complaining if there is none. */
tm_t *tm_arg(const char *sub, const char *t)
{
	tm_t *m = t ? tm_find(atoi(t)) : 0;

	if (!m)
		lg(HIBR_LERR, "term %s: %s: no such terminal", sub,
		   t ? t : "");
	return m;
}

/* Hand a value back through the result slot, printing it only when nobody
   asked for it. */
void tm_ret(sh *s, const char *t)
{
	hibr_ret(s, t);
	if (!s->bind)
		printf("%s\n", t);
}

/* Collect whatever the program has written and run it through the parser. */
int tm_pump(tm_t *m, int ms)
{
	str b;
	int r;

	if (!tm_pty || !m->pty)
		return -1;
	s_init(&b);
	r = tm_pty->read(m->pty, ms, &b);
	if (b.n)
		tm_feed(m, b.p, b.n);
	s_free(&b);
	if (r < 0)
		m->done = 1;
	return r;
}

/* Run a program on a terminal of its own and keep a picture of its screen. */
int m_term(sh *s, int ac, char **av)
{
	const char *sub = ac > 1 ? av[1] : "";
	int rows = 24, cols = 80, lines = 1000, i, r;
	tm_t *m;
	str o;

	if (ac < 2) {
		lg(HIBR_LERR, "usage: term open|poll|draw|key|write|size|"
			      "alive|status|title|cursor|row|scroll|mouse|screen|select|copy|close ...");
		return 2;
	}
	if (!strcmp(sub, "open")) {
		if (!tm_pty) {
			lg(HIBR_LERR, "term: no pty module");
			return HIBR_FAIL;
		}
		i = 2;
		while (i < ac && av[i][0] == '-' && av[i][1]) {
			if (!strcmp(av[i], "-r") && i + 1 < ac)
				rows = atoi(av[++i]);
			else if (!strcmp(av[i], "-c") && i + 1 < ac)
				cols = atoi(av[++i]);
			else if (!strcmp(av[i], "-s") && i + 1 < ac)
				lines = atoi(av[++i]);
			else if (!strcmp(av[i], "--")) {
				i++;
				break;
			} else {
				lg(HIBR_LERR, "term open: %s: unknown option",
				   av[i]);
				return 2;
			}
			i++;
		}
		if (i >= ac) {
			lg(HIBR_LERR, "usage: term open [-r rows] [-c cols] "
				      "[-s lines] command [args...]");
			return 2;
		}
		if (rows < 1 || cols < 1) {
			lg(HIBR_LERR, "term open: size must be positive");
			return 2;
		}
		m = tm_new(rows, cols);
		m->sbmax = lines < 0 ? 0 : lines;
		m->pty = tm_pty->spawn(s, rows, cols, av + i);
		if (!m->pty) {
			tm_free(m);
			return HIBR_FAIL;
		}
		s_init(&o);
		s_num(&o, (long)m->id);
		tm_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (ac < 3) {
		lg(HIBR_LERR, "term %s: which terminal?", sub);
		return 2;
	}
	m = tm_arg(sub, av[2]);
	if (!m)
		return HIBR_FAIL;

	if (!strcmp(sub, "poll")) {
		r = tm_pump(m, ac > 3 ? atoi(av[3]) : 0);
		return r < 0 ? HIBR_FAIL : HIBR_OK;
	}
	if (!strcmp(sub, "draw")) {
		if (!tm_dp) {
			lg(HIBR_LERR, "term draw: no display");
			return HIBR_FAIL;
		}
		if (ac < 5) {
			lg(HIBR_LERR, "usage: term draw id row col [h] [w] [curon]");
			return 2;
		}
		tm_draw(m, tm_dp, atoi(av[3]), atoi(av[4]),
			ac > 5 ? atoi(av[5]) : 0, ac > 6 ? atoi(av[6]) : 0,
			ac > 7 ? atoi(av[7]) : 0);
		return HIBR_OK;
	}
	if (!strcmp(sub, "key")) {
		if (ac < 4) {
			lg(HIBR_LERR, "usage: term key id name");
			return 2;
		}
		s_init(&o);
		if (m->bpaste && !strncmp(av[3], "paste ", 6))
			s_cat(&o, "\033[200~");
		r = tm_keybytes(av[3], &o);
		if (r && m->bpaste && !strncmp(av[3], "paste ", 6))
			s_cat(&o, "\033[201~");
		if (r && tm_pty) {
			tm_pty->write(m->pty, o.p, o.n);
			m->view = 0;
		}
		s_free(&o);
		return r ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "write")) {
		if (ac < 4) {
			lg(HIBR_LERR, "usage: term write id text...");
			return 2;
		}
		for (i = 3; i < ac; i++)
			tm_pty->write(m->pty, av[i], strlen(av[i]));
		m->view = 0;
		return HIBR_OK;
	}
	if (!strcmp(sub, "size")) {
		if (ac >= 5) {
			rows = atoi(av[3]);
			cols = atoi(av[4]);
			if (!tm_size(m, rows, cols))
				return HIBR_FAIL;
			if (tm_pty)
				tm_pty->resize(m->pty, rows, cols);
			return HIBR_OK;
		}
		s_init(&o);
		s_num(&o, (long)m->rows);
		s_ch(&o, ' ');
		s_num(&o, (long)m->cols);
		tm_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "alive")) {
		tm_pump(m, 0);
		if (!tm_pty)
			return HIBR_FAIL;
		return tm_pty->alive(m->pty) ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "status")) {
		s_init(&o);
		s_num(&o, (long)(tm_pty ? tm_pty->status(m->pty) : -1));
		tm_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "title")) {
		tm_ret(s, m->title.p ? m->title.p : "");
		return HIBR_OK;
	}
	if (!strcmp(sub, "cursor")) {
		if (ac > 3) {
			if (!strcmp(av[3], "block"))
				m->cshape = TM_BLOCK;
			else if (!strcmp(av[3], "underline"))
				m->cshape = TM_UNDER;
			else if (!strcmp(av[3], "bar"))
				m->cshape = TM_BAR;
			else {
				lg(HIBR_LERR, "term cursor: %s: block, "
					      "underline or bar", av[3]);
				return 2;
			}
			return HIBR_OK;
		}
		s_init(&o);
		s_num(&o, (long)m->cr);
		s_ch(&o, ' ');
		s_num(&o, (long)m->cc);
		s_ch(&o, ' ');
		s_num(&o, (long)m->vis);
		s_ch(&o, ' ');
		s_cat(&o, m->cshape == TM_UNDER ? "underline" :
			  m->cshape == TM_BAR ? "bar" : "block");
		tm_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "row")) {
		int c;
		tm_cell *k;
		if (ac < 4) {
			lg(HIBR_LERR, "usage: term row id n");
			return 2;
		}
		r = atoi(av[3]);
		s_init(&o);
		for (c = 0; c < m->cols; c++) {
			k = tm_vat(m, r, c);
			if (!k || !k->w)
				continue;
			tm_utf8(&o, k->cp ? k->cp : ' ');
		}
		while (o.n && o.p[o.n - 1] == ' ')
			o.p[--o.n] = 0;
		tm_ret(s, o.p ? o.p : "");
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "scroll")) {
		if (ac > 3) {
			if (!strcmp(av[3], "top"))
				tm_view(m, m->sbn);
			else if (!strcmp(av[3], "bottom"))
				tm_view(m, -m->view);
			else
				tm_view(m, atoi(av[3]));
			return HIBR_OK;
		}
		s_init(&o);
		s_num(&o, (long)m->view);
		s_ch(&o, ' ');
		s_num(&o, (long)m->sbn);
		tm_ret(s, o.p);
		s_free(&o);
		return HIBR_OK;
	}
	if (!strcmp(sub, "mouse")) {
		const char *btn = "";
		int at = 4;
		if (ac < 4) {
			tm_ret(s, tm_mname(m));
			return HIBR_OK;
		}
		if (strncmp(av[3], "wheel", 5))
			btn = ac > at ? av[at++] : "";
		if (ac < at + 2) {
			lg(HIBR_LERR, "usage: term mouse id [press|release|"
				      "drag button|wheelup|wheeldown] row col");
			return 2;
		}
		s_init(&o);
		r = tm_mouse(m, av[3], btn, atoi(av[at]), atoi(av[at + 1]), &o);
		if (r && o.n && tm_pty) {
			tm_pty->write(m->pty, o.p, o.n);
			m->view = 0;
		}
		s_free(&o);
		return r ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "select")) {
		if (ac > 3 && !strcmp(av[3], "none")) {
			m->sel = 0;
			return HIBR_OK;
		}
		if (ac < 6 || (strcmp(av[3], "start") && strcmp(av[3], "to"))) {
			lg(HIBR_LERR, "usage: term select id start|to row col, "
				      "or term select id none");
			return 2;
		}
		tm_selset(m, atoi(av[4]), atoi(av[5]), av[3][0] == 's');
		return HIBR_OK;
	}
	if (!strcmp(sub, "copy")) {
		s_init(&o);
		tm_seltext(m, &o);
		tm_ret(s, o.p ? o.p : "");
		s_free(&o);
		return m->sel ? HIBR_OK : HIBR_FAIL;
	}
	if (!strcmp(sub, "screen")) {
		tm_ret(s, m->inalt ? "alt" : "main");
		return HIBR_OK;
	}
	if (!strcmp(sub, "close")) {
		if (tm_pty && m->pty)
			tm_pty->drop(m->pty);
		tm_free(m);
		return HIBR_OK;
	}
	lg(HIBR_LERR, "term: %s: unknown subcommand", sub);
	return HIBR_FAIL;
}

/* Find the pty and the display this needs to do anything at all. */
int tm_ini(sh *s)
{
	tm_pty = (const py_api *)hibr_require(s, "pty", PY_API_VER);
	if (!tm_pty) {
		lg(HIBR_LERR, "term: needs the pty module");
		return HIBR_FAIL;
	}
	tm_dp = (const dp_api *)hibr_require(s, "display", DP_API_VER);
	if (!tm_dp)
		lg(HIBR_LWRN, "term: no display yet; term draw will fail");
	return hibr_provide(s, "terminal", 1u, (void *)&m_term);
}

/* Close every terminal and the programs behind them. */
void tm_fini(sh *s)
{
	while (tm_list.n) {
		tm_t *m = (tm_t *)tm_list.p[tm_list.n - 1];
		if (tm_pty && m->pty)
			tm_pty->drop(m->pty);
		tm_free(m);
	}
	v_free(&tm_list);
	hibr_unprovide(s, "terminal");
}

const hibr_bi term_bi[] = {
	{ "term", m_term, "run a program on a terminal and draw its screen" },
	HIBR_BI_END
};

HIBR_MODULE_P("term", "0.21",
	      "a terminal emulator: a program's screen as cells",
	      term_bi, tm_ini, tm_fini, "terminal");
