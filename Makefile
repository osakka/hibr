CC = tcc
TLS ?= 1
OPT ?=
CFLAGS = -Iinclude -Wall $(OPT)
UNAME := $(shell uname -s)
ifeq ($(TLS),1)
CFLAGS += -DHIBR_TLS
endif

# Darwin keeps dlopen in libc, spells -rdynamic differently, and builds shared
# objects with -dynamiclib. Everything else uses the Linux and BSD spelling.
ifeq ($(UNAME),Darwin)
LDFLAGS = -Wl,-export_dynamic
SOFLAGS = -dynamiclib -undefined dynamic_lookup
else
LDFLAGS = -rdynamic -ldl
SOFLAGS = -shared
endif

B = build
BIN = $(B)/hibr
SRC = src/mem.c src/var.c src/lex.c src/parse.c src/expand.c src/exec.c \
      src/bi.c src/mod.c src/job.c src/trap.c src/regex.c src/daily.c src/net.c src/json.c src/text.c src/args.c src/edit.c src/main.c
MODS = $(B)/mods/sys.so $(B)/mods/http.so $(B)/mods/ls.so $(B)/mods/prompt.so \
       $(B)/mods/screen.so $(B)/mods/cat.so \
       $(B)/mods/trace.so
PROMPT_SRC = $(wildcard mods/prompt/*.c)
SCREEN_SRC = $(wildcard mods/screen/*.c)
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

$(B)/mods/screen.so: $(SCREEN_SRC) include/hibr.h mods/screen/scr.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(SCREEN_SRC)

$(B)/mods/cat.so: $(CAT_SRC) include/hibr.h mods/cat/ct.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $(CAT_SRC)

$(B)/mods/trace.so: mods/trace/trace.c include/hibr.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ mods/trace/trace.c

$(B)/mods/%.so: mods/%.c include/hibr.h | $(B)/mods
	$(CC) $(CFLAGS) $(SOFLAGS) -o $@ $<

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
