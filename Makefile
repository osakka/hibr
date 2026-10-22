TLS ?= 1
OPT ?=
CFLAGS = -Iinclude -Wall $(OPT)
UNAME := $(shell uname -s)
ifeq ($(TLS),1)
CFLAGS += -DHIBR_TLS
endif

# tcc does not build on Darwin at all, so the default there is the system
# compiler instead -- cc, which Xcode's command line tools point at clang.
# origin, not ?=: make's own built-in default already sets CC to "cc" before
# this file is even read, which makes ?= a no-op here and CC always "cc" --
# this only replaces that built-in default, so an explicit make CC=... or
# CC=... in the environment still wins over either platform's default.
ifeq ($(origin CC),default)
ifeq ($(UNAME),Darwin)
CC = cc
else
CC = tcc
endif
endif

# Darwin keeps dlopen in libc, spells -rdynamic differently, and builds shared
# objects with -dynamiclib. Everything else uses the Linux and BSD spelling.
ifeq ($(UNAME),Darwin)
LDFLAGS = -Wl,-export_dynamic
SOFLAGS = -dynamiclib -undefined dynamic_lookup
DLLIB =
else
LDFLAGS = -rdynamic -ldl
SOFLAGS = -shared -fPIC
DLLIB = -ldl
endif

B = build
BIN = $(B)/hibr
SRC = src/mem.c src/var.c src/lex.c src/parse.c src/expand.c src/exec.c \
      src/bi.c src/mod.c src/job.c src/trap.c src/regex.c src/daily.c src/net.c src/json.c src/text.c src/args.c src/edit.c src/plan.c src/fs.c src/wtab.c src/main.c
MODS = $(B)/mods/sys.so $(B)/mods/http.so $(B)/mods/ls.so $(B)/mods/prompt.so \
       $(B)/mods/console.so $(B)/mods/cat.so \
       $(B)/mods/trace.so $(B)/mods/most.so \
       $(B)/mods/hvi.so $(B)/mods/mon.so \
       $(B)/mods/sysinfo.so $(B)/mods/pty.so \
       $(B)/mods/term.so $(B)/mods/hold.so $(B)/mods/img.so \
       $(B)/mods/lint.so $(B)/mods/db.so $(B)/mods/md.so $(B)/mods/pim.so $(B)/mods/html.so $(B)/mods/email.so $(B)/mods/math.so $(B)/mods/web.so \
       $(B)/mods/dav.so $(B)/mods/media.so $(B)/mods/auth.so $(B)/mods/csv.so $(B)/mods/lines.so $(B)/mods/uni.so $(B)/mods/lang.so $(B)/mods/hcal.so $(B)/mods/salat.so \
       $(B)/mods/vw.so $(B)/mods/archive.so $(B)/mods/mermaid.so \
       $(B)/mods/xscr.so $(B)/mods/pdf.so

# darwin.c reaches Mach headers (mach/mach.h and kin) that only exist on
# Darwin at all, so it is not in MODS above and never built, or even asked
# to build, anywhere else -- there is nothing to guard inside the file
# itself with #ifdef, and nothing on Linux ever tries to compile it.
ifeq ($(UNAME),Darwin)
MODS += $(B)/mods/darwin.so
endif
PROMPT_SRC = $(wildcard mods/prompt/*.c) mods/inflate.c mods/inftab.c
# The console compresses a still picture before sending it (Gitea #129), so
# it carries the deflate encoder -- and inftab.c, the tables both directions
# read, rather than the whole of inflate.c: it never expands anything, and
# the decoder is six kilobytes of code it would not call.
CONSOLE_SRC = $(wildcard mods/console/*.c) mods/deflate.c mods/inftab.c \
	      mods/png.c mods/gfxask.c
PTY_SRC = $(wildcard mods/pty/*.c)
TERM_SRC = $(wildcard mods/term/*.c)
HOLD_SRC = $(wildcard mods/hold/*.c) mods/gfxask.c
CAT_SRC = $(wildcard mods/cat/*.c)

PREFIX ?= /usr/local
# Which of the desktop's installables `make install` places: what
# packaging/ships lists and the parts those bring, and nothing else
# (Gitea #271, ADR 0047). tools/ships.hibr answers it, run by the shell
# just built, so a package, a source install, Homebrew and the AUR all
# hold the same set without any of them naming it.
#
# SHIP=all places every one, for a machine that will never reach a
# catalogue; `make install-extras` places exactly the ones the default
# leaves out.
SHIP ?= listed
MODDIR ?= $(PREFIX)/lib/hibr
# Where an administrator's catalogue installs for every user of the
# machine (ADR 0045). Not under PREFIX on purpose: `make install` does
# `rm -rf $(DESTDIR)$(DESKDIR)`, so a system scope under the install
# prefix would be deleted by the next source install, silently -- and
# $(MODDIR) is the package's, so a catalogue writing there would put two
# package managers in one directory. /var/lib is the FHS place for state
# a program maintains, which is what a catalogue's store is.
# ...when the program is installed where the FHS expects one. A
# **prefixed** install is self-contained by convention and has no
# business creating a directory outside its own prefix -- which is
# exactly what `make install PREFIX=/opt/homebrew` did: `install -d
# /var/lib/hibr`, which on a Mac is root-owned and not Homebrew's to
# touch, so every `brew install hibr` failed with permission denied.
# That is a hibr bug and not a Homebrew one; every other path here has
# always derived from PREFIX and this one did not.
#
# `/usr/local` is deliberately **not** in the FHS list: Homebrew's own
# prefix on an Intel Mac is exactly that, so putting it there would fix
# Apple Silicon and leave Intel broken in the same way.
SYSDIR ?= $(if $(filter /usr,$(PREFIX)),/var/lib/hibr,$(PREFIX)/var/lib/hibr)
SHAREDIR ?= $(PREFIX)/share/hibr
MANDIR ?= $(PREFIX)/share/man
DESKDIR = $(SHAREDIR)/desktop
SHCFLAGS = $(CFLAGS) -DHIBR_MODDIR=\"$(MODDIR)\" -DHIBR_SYSDIR=\"$(SYSDIR)\"

all: $(BIN) $(MODS) $(B)/hibr.1 $(B)/desktop.1

# The manual page, with the version and the module directory filled in from
# the same places the binary takes them, so neither can go stale in it.
$(B)/hibr.1: docs/hibr.1.in include/hibr.h $(B)/.moddir | $(B)/mods
	sed -e "s|@VERSION@|$$(sed -n 's/^#define HIBR_VER "\(.*\)"/\1/p' include/hibr.h)|" \
	    -e 's|@MODDIR@|$(MODDIR)|' docs/hibr.1.in > $@

# And the desktop's, with the version and where the desktop is installed
# (Gitea #344).
$(B)/desktop.1: docs/desktop.1.in include/hibr.h | $(B)/mods
	sed -e "s|@VERSION@|$$(sed -n 's/^#define HIBR_VER "\(.*\)"/\1/p' include/hibr.h)|" \
	    -e 's|@DESKDIR@|$(DESKDIR)|' docs/desktop.1.in > $@

$(B)/mods:
	@mkdir -p $@

# The module directory and the system scope are compiled into the shell, so
# a change of PREFIX, MODDIR or SYSDIR has to force a rebuild; this stamp
# only changes when one of the values does.
$(B)/.moddir: FORCE | $(B)/mods
	@echo '$(MODDIR) $(SYSDIR)' | cmp -s - $@ 2>/dev/null || \
		echo '$(MODDIR) $(SYSDIR)' > $@
FORCE:

$(BIN): $(SRC) include/hibr.h include/pri.h mods/lint.h $(B)/.moddir | $(B)/mods
	$(CC) $(SHCFLAGS) $(LDFLAGS) -o $@ $(SRC)

$(B)/mods/prompt.so: $(PROMPT_SRC) include/hibr.h mods/prompt/pr.h mods/inflate.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(PROMPT_SRC)

$(B)/mods/pty.so: $(PTY_SRC) include/hibr.h mods/pty/tt.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(PTY_SRC)

$(B)/mods/term.so: $(TERM_SRC) include/hibr.h mods/term/tm.h mods/pty.h mods/display.h mods/term.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(TERM_SRC)

$(B)/mods/hold.so: $(HOLD_SRC) include/hibr.h mods/hold/hd.h mods/pty.h mods/term.h mods/display.h mods/gfxask.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(HOLD_SRC)

$(B)/mods/console.so: $(CONSOLE_SRC) include/hibr.h mods/console/cn.h mods/display.h mods/inflate.h mods/png.h mods/gfxask.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(CONSOLE_SRC)

$(B)/mods/cat.so: $(CAT_SRC) include/hibr.h mods/cat/ct.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(CAT_SRC)

HVI_SRC = $(wildcard mods/hvi/*.c)
MON_SRC = $(wildcard mods/mon/*.c)

$(B)/mods/sysinfo.so: mods/sysinfo/sysinfo.c include/hibr.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ mods/sysinfo/sysinfo.c

$(B)/mods/db.so: mods/db/db.c include/hibr.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ mods/db/db.c

$(B)/mods/mon.so: $(MON_SRC) include/hibr.h mods/mon/mn.h mods/display.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(MON_SRC)

$(B)/mods/hvi.so: $(HVI_SRC) include/hibr.h mods/hvi/vi.h mods/display.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(HVI_SRC)

$(B)/mods/most.so: mods/most/most.c include/hibr.h mods/display.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ mods/most/most.c

TRACE_SRC = $(wildcard mods/trace/*.c)

$(B)/mods/trace.so: $(TRACE_SRC) include/hibr.h mods/trace/tr.h mods/display.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(TRACE_SRC)

LINT_SRC = $(wildcard mods/lint/*.c)

$(B)/mods/lint.so: $(LINT_SRC) include/hibr.h mods/lint.h $(B)/.moddir | $(B)/mods
	$(CC) $(SHCFLAGS) $(SOFLAGS) -o $@ $(LINT_SRC)

# Calendar systems are data: the module reads them from the installed
# folder too, so it is told where that is, the way lint is told MODDIR.
$(B)/mods/hcal.so: mods/hcal/hcal.c include/hibr.h | $(B)/mods
	$(CC) $(CFLAGS) -DHIBR_SHAREDIR=\"$(SHAREDIR)\" $(SOFLAGS) -o $@ mods/hcal/hcal.c

$(B)/mods/salat.so: mods/salat/salat.c include/hibr.h | $(B)/mods
	$(CC) $(CFLAGS) -DHIBR_SHAREDIR=\"$(SHAREDIR)\" $(SOFLAGS) -o $@ mods/salat/salat.c -lm

$(B)/mods/vw.so: mods/vw/vw.c include/hibr.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ mods/vw/vw.c $(DLLIB)

# The inflate is mods/inflate.c, the prompt module's own, shared rather than
# written twice (Gitea #102), with mods/inftab.c beside it for the tables
# the encoder reads as well.
$(B)/mods/archive.so: mods/archive/archive.c mods/inflate.c mods/inftab.c mods/inflate.h include/hibr.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ mods/archive/archive.c mods/inflate.c mods/inftab.c

MERMAID_SRC = $(wildcard mods/mermaid/*.c)

$(B)/mods/mermaid.so: $(MERMAID_SRC) include/hibr.h mods/mermaid/mm.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(MERMAID_SRC)

$(B)/mods/math.so: mods/math.c include/hibr.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ mods/math.c -lm

XSCR_SRC = $(wildcard mods/xscr/*.c)

$(B)/mods/xscr.so: $(XSCR_SRC) include/hibr.h mods/display.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(XSCR_SRC)

MEDIA_SRC = $(wildcard mods/media/*.c)

$(B)/mods/media.so: $(MEDIA_SRC) include/hibr.h mods/media/mv.h mods/display.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(MEDIA_SRC) $(DLLIB) -lpthread

DAV_SRC = $(wildcard mods/dav/*.c)

$(B)/mods/dav.so: $(DAV_SRC) include/hibr.h mods/dav/dv.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(DAV_SRC) $(DLLIB)

WEB_SRC = $(wildcard mods/web/*.c) mods/img/png.c

$(B)/mods/web.so: $(WEB_SRC) include/hibr.h mods/web/wb.h mods/img/im.h mods/display.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(WEB_SRC) $(DLLIB)

EMAIL_SRC = $(wildcard mods/email/*.c)

$(B)/mods/email.so: $(EMAIL_SRC) include/hibr.h mods/email/ml.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(EMAIL_SRC) $(DLLIB)

HTML_SRC = $(wildcard mods/html/*.c)

$(B)/mods/html.so: $(HTML_SRC) include/hibr.h mods/html/hl.h mods/html/tr.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(HTML_SRC)

PIM_SRC = $(wildcard mods/pim/*.c)

$(B)/mods/pim.so: $(PIM_SRC) include/hibr.h mods/pim/pm.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(PIM_SRC)

UNI_SRC = $(wildcard mods/uni/*.c)

$(B)/mods/uni.so: $(UNI_SRC) include/hibr.h mods/uni/un.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(UNI_SRC)

MD_SRC = $(wildcard mods/md/*.c)

$(B)/mods/md.so: $(MD_SRC) include/hibr.h mods/md/mk.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(MD_SRC)

# PDF through Poppler, every library dlopen'd (Gitea #348).
$(B)/mods/pdf.so: mods/pdf/pdf.c include/hibr.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ mods/pdf/pdf.c $(DLLIB)

IMG_SRC = $(wildcard mods/img/*.c)

$(B)/mods/img.so: $(IMG_SRC) include/hibr.h mods/img/im.h mods/display.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(IMG_SRC) $(DLLIB)

$(B)/mods/%.so: mods/%.c include/hibr.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $<

# The module names this build makes, for deploy.sh to record and compare
# against. Globbing build/mods would also find ones left over from a rename.
print-mods:
	@for m in $(MODS); do b=$${m##*/}; printf '%s ' "$${b%.so}"; done; echo

strip: $(BIN)
	strip $(BIN) || true

check: all
	tests/run.sh

# The shell and every module under AddressSanitizer and UBSan, in
# build/asan; for tests/asan.py, which runs every suite against it.
ASAN = -g -O1 -w -fPIC -fsanitize=address,undefined -fno-sanitize-recover=undefined
asan:
	$(MAKE) B=$(B)/asan CC=gcc OPT="$(ASAN)" all

# The shell again, logging every function call to $HIBR_CENSUS; for
# tests/census.py. Compiled out of the real one, which pays nothing for it.
census: $(B)/hibr.census $(MODS)

$(B)/hibr.census: $(SRC) include/hibr.h include/pri.h $(B)/.moddir | $(B)/mods
	$(CC) $(SHCFLAGS) -DHIBR_CENSUS $(LDFLAGS) -o $@ $(SRC)

# Every suite, the pty ones included, side by side: a couple of minutes.
check-all: all
	python3 tests/all.py

# What HIBR_VER and the desktop's own DT_VER should probably become next,
# from what actually changed since the last release tag (or FROM, given
# explicitly) -- see tools/next-version.sh's own comment for what decides
# minor from major. Suggests; release.sh still does the real bump, with
# its own gates (clean tree, CHANGELOG entry, passing tests) intact.
next-version:
	tools/next-version.sh $(FROM)

# Exactly what a default install leaves out, where `make install
# SHIP=all` would have put it: for a machine that will never reach a
# catalogue, and for the suite that checks the two halves add up.
install-extras: all
	$(BIN) tools/ships.hibr -n > $(B)/extras.txt
	while IFS= read -r f; do \
		mkdir -p "$(DESTDIR)$(DESKDIR)/$${f%/*}" && \
		cp "programs/desktop/$$f" "$(DESTDIR)$(DESKDIR)/$$f" || exit 1; \
	done < $(B)/extras.txt

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(MODDIR) $(DESTDIR)$(PREFIX)/include/hibr
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/hibr
	install -m 644 $(MODS) $(DESTDIR)$(MODDIR)
	install -m 644 include/hibr.h $(DESTDIR)$(PREFIX)/include/hibr/hibr.h
	install -d $(DESTDIR)$(MANDIR)/man1
	install -m 644 $(B)/hibr.1 $(DESTDIR)$(MANDIR)/man1/hibr.1
	install -m 644 $(B)/desktop.1 $(DESTDIR)$(MANDIR)/man1/desktop.1
	install -d $(DESTDIR)$(SHAREDIR)/calendars
	install -m 644 mods/hcal/calendars/*.json $(DESTDIR)$(SHAREDIR)/calendars
	install -d $(DESTDIR)$(SHAREDIR)/salat
	install -m 644 mods/salat/methods/*.json $(DESTDIR)$(SHAREDIR)/salat
	rm -rf $(DESTDIR)$(DESKDIR)
	install -d $(DESTDIR)$(DESKDIR)
	cp -R programs/desktop/. $(DESTDIR)$(DESKDIR)/
# The default takes out what packaging/ships does not list and no listed
# file brings -- the applications that come from the catalogue, their
# panes and strip modules, and every theme and colour scheme but Classic,
# Midnight and Paper (ADR 0047) -- and then the folders that leaves empty.
# The tool's list goes to a file first, so a tool that fails stops the
# install rather than taking nothing out.
ifneq ($(SHIP),all)
	$(BIN) tools/ships.hibr -n > $(B)/extras.txt
	while IFS= read -r f; do \
		rm -f "$(DESTDIR)$(DESKDIR)/$$f" || exit 1; \
	done < $(B)/extras.txt
	find $(DESTDIR)$(DESKDIR)/apps -depth -type d -empty -exec rmdir {} \;
endif
	sed 's|@DESKDIR@|$(DESKDIR)|' tools/desktop-launcher.in \
		> $(DESTDIR)$(PREFIX)/bin/desktop
	chmod 755 $(DESTDIR)$(PREFIX)/bin/desktop
	install -m 644 programs/vw.hibr $(DESTDIR)$(SHAREDIR)/vw.hibr
	sed 's|@SHAREDIR@|$(SHAREDIR)|' tools/vw-launcher.in \
		> $(DESTDIR)$(PREFIX)/bin/vw
	chmod 755 $(DESTDIR)$(PREFIX)/bin/vw
	install -m 644 packaging/hibr-apps.gpg $(DESTDIR)$(SHAREDIR)/keyring.gpg
# `apps` was this command's name until 0.99.195. dpkg takes a file the new
# package no longer ships, but `make install` does not -- so without this a
# source upgrade leaves `bin/apps` behind pointing at a `share/hibr/apps.hibr`
# that is gone, which is a command that exists and fails. Worse than absent.
	rm -f $(DESTDIR)$(PREFIX)/bin/apps $(DESTDIR)$(SHAREDIR)/apps.hibr
# The system scope's root, made here so `catalogue --system` has somewhere
# to write and so the ownership the module search insists on is set by the
# thing that creates it rather than hoped for later: root-owned, and not
# writable by anybody else (ADR 0045). 0755 rather than 0700 because every
# user of the machine reads it.
#
# MKSYSDIR=no leaves it to the catalogue, which makes it the first time a
# `--system` install writes there: the Homebrew formula puts the system
# scope in Homebrew's own var/, which outlives an upgrade where its keg
# does not, and an install step writing outside the keg is not one its
# build sandbox can be counted on to allow (Gitea #268).
ifneq ($(MKSYSDIR),no)
	install -d -m 755 $(DESTDIR)$(SYSDIR)
endif
	install -m 644 programs/catalogue.hibr $(DESTDIR)$(SHAREDIR)/catalogue.hibr
	sed 's|@SHAREDIR@|$(SHAREDIR)|' tools/catalogue-launcher.in \
		> $(DESTDIR)$(PREFIX)/bin/catalogue
	chmod 755 $(DESTDIR)$(PREFIX)/bin/catalogue

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/hibr $(DESTDIR)$(PREFIX)/bin/desktop \
		$(DESTDIR)$(PREFIX)/bin/vw $(DESTDIR)$(SHAREDIR)/vw.hibr \
		$(DESTDIR)$(PREFIX)/bin/catalogue \
		$(DESTDIR)$(SHAREDIR)/catalogue.hibr \
		$(DESTDIR)$(PREFIX)/bin/apps $(DESTDIR)$(SHAREDIR)/apps.hibr \
		$(DESTDIR)$(MANDIR)/man1/hibr.1 \
		$(DESTDIR)$(MANDIR)/man1/desktop.1
	rm -rf $(DESTDIR)$(MODDIR) $(DESTDIR)$(PREFIX)/include/hibr \
		$(DESTDIR)$(DESKDIR)
# Only if nothing was installed into it. What a catalogue put there is an
# administrator's, not this Makefile's to delete -- `catalogue --system
# remove` is how that goes.
	-rmdir $(DESTDIR)$(SYSDIR) 2> /dev/null

clean:
	rm -rf $(B)

.PHONY: all clean strip check check-all census asan next-version install install-extras uninstall FORCE
