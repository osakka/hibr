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
SOFLAGS = -shared
DLLIB = -ldl
endif

B = build
BIN = $(B)/hibr
SRC = src/mem.c src/var.c src/lex.c src/parse.c src/expand.c src/exec.c \
      src/bi.c src/mod.c src/job.c src/trap.c src/regex.c src/daily.c src/net.c src/json.c src/text.c src/args.c src/edit.c src/main.c
MODS = $(B)/mods/sys.so $(B)/mods/http.so $(B)/mods/ls.so $(B)/mods/prompt.so \
       $(B)/mods/console.so $(B)/mods/cat.so \
       $(B)/mods/trace.so $(B)/mods/most.so \
       $(B)/mods/hvi.so $(B)/mods/mon.so \
       $(B)/mods/sysinfo.so $(B)/mods/pty.so \
       $(B)/mods/term.so $(B)/mods/hold.so $(B)/mods/img.so
PROMPT_SRC = $(wildcard mods/prompt/*.c)
CONSOLE_SRC = $(wildcard mods/console/*.c)
PTY_SRC = $(wildcard mods/pty/*.c)
TERM_SRC = $(wildcard mods/term/*.c)
HOLD_SRC = $(wildcard mods/hold/*.c)
CAT_SRC = $(wildcard mods/cat/*.c)

PREFIX ?= /usr/local
MODDIR ?= $(PREFIX)/lib/hibr
SHCFLAGS = $(CFLAGS) -DHIBR_MODDIR=\"$(MODDIR)\"

all: $(BIN) $(MODS)

$(B)/mods:
	@mkdir -p $@

# The module directory is compiled into the shell, so a change of PREFIX or
# MODDIR has to force a rebuild; this stamp only changes when the value does.
$(B)/.moddir: FORCE | $(B)/mods
	@echo '$(MODDIR)' | cmp -s - $@ 2>/dev/null || echo '$(MODDIR)' > $@
FORCE:

$(BIN): $(SRC) include/hibr.h include/pri.h $(B)/.moddir | $(B)/mods
	$(CC) $(SHCFLAGS) $(LDFLAGS) -o $@ $(SRC)

$(B)/mods/prompt.so: $(PROMPT_SRC) include/hibr.h mods/prompt/pr.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(PROMPT_SRC)

$(B)/mods/pty.so: $(PTY_SRC) include/hibr.h mods/pty/tt.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(PTY_SRC)

$(B)/mods/term.so: $(TERM_SRC) include/hibr.h mods/term/tm.h mods/pty.h mods/display.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(TERM_SRC)

$(B)/mods/hold.so: $(HOLD_SRC) include/hibr.h mods/hold/hd.h mods/pty.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(HOLD_SRC)

$(B)/mods/console.so: $(CONSOLE_SRC) include/hibr.h mods/console/cn.h mods/display.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(CONSOLE_SRC)

$(B)/mods/cat.so: $(CAT_SRC) include/hibr.h mods/cat/ct.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(CAT_SRC)

HVI_SRC = $(wildcard mods/hvi/*.c)
MON_SRC = $(wildcard mods/mon/*.c)

$(B)/mods/sysinfo.so: mods/sysinfo/sysinfo.c include/hibr.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ mods/sysinfo/sysinfo.c

$(B)/mods/mon.so: $(MON_SRC) include/hibr.h mods/mon/mn.h mods/display.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(MON_SRC)

$(B)/mods/hvi.so: $(HVI_SRC) include/hibr.h mods/hvi/vi.h mods/display.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(HVI_SRC)

$(B)/mods/most.so: mods/most/most.c include/hibr.h mods/display.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ mods/most/most.c

TRACE_SRC = $(wildcard mods/trace/*.c)

$(B)/mods/trace.so: $(TRACE_SRC) include/hibr.h mods/trace/tr.h mods/display.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(TRACE_SRC)

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

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(MODDIR) $(DESTDIR)$(PREFIX)/include/hibr
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/hibr
	install -m 644 $(MODS) $(DESTDIR)$(MODDIR)
	install -m 644 include/hibr.h $(DESTDIR)$(PREFIX)/include/hibr/hibr.h

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/hibr
	rm -rf $(DESTDIR)$(MODDIR) $(DESTDIR)$(PREFIX)/include/hibr

clean:
	rm -rf $(B)

.PHONY: all clean strip check install uninstall FORCE
