CC = tcc
TLS ?= 1
CFLAGS = -Iinclude -Wall
LDFLAGS = -rdynamic -ldl
ifeq ($(TLS),1)
CFLAGS += -DHIBR_TLS
endif
SRC = src/mem.c src/var.c src/lex.c src/parse.c src/expand.c src/exec.c \
      src/bi.c src/mod.c src/job.c src/trap.c src/regex.c src/daily.c src/net.c src/json.c src/text.c src/args.c src/edit.c src/main.c
MODS = mods/sys.so mods/http.so mods/ls.so mods/prompt.so
PROMPT_SRC = $(wildcard mods/prompt/*.c)

PREFIX ?= /usr/local
MODDIR ?= $(PREFIX)/lib/hibr
SHCFLAGS = $(CFLAGS) -DHIBR_MODDIR=\"$(MODDIR)\"

all: hibr $(MODS)

# The module directory is compiled into the shell, so a change of PREFIX or
# MODDIR has to force a rebuild; this stamp only changes when the value does.
.moddir: FORCE
	@echo '$(MODDIR)' | cmp -s - $@ 2>/dev/null || echo '$(MODDIR)' > $@
FORCE:

hibr: $(SRC) include/hibr.h include/pri.h .moddir
	$(CC) $(SHCFLAGS) $(LDFLAGS) -o $@ $(SRC)

mods/prompt.so: $(PROMPT_SRC) include/hibr.h mods/prompt/pr.h
	$(CC) $(CFLAGS) -shared -o $@ $(PROMPT_SRC)

mods/%.so: mods/%.c include/hibr.h
	$(CC) $(CFLAGS) -shared -o $@ $<

strip: hibr
	strip hibr || true

check: all
	tests/run.sh

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(MODDIR) $(DESTDIR)$(PREFIX)/include/hibr
	install -m 755 hibr $(DESTDIR)$(PREFIX)/bin/hibr
	install -m 644 $(MODS) $(DESTDIR)$(MODDIR)
	install -m 644 include/hibr.h $(DESTDIR)$(PREFIX)/include/hibr/hibr.h

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/hibr
	rm -rf $(DESTDIR)$(MODDIR) $(DESTDIR)$(PREFIX)/include/hibr

clean:
	rm -f hibr mods/*.so .moddir

.PHONY: all clean strip check install uninstall FORCE
