# The desktop ships bare (Gitea #220): `hibr` is the window manager and
# everything that manages it, and nothing you open -- not Terminal and
# not Files, since the owner's line is that nothing stays. There is one
# package, and the applications come from the catalogue: `apps install
# apps/` puts them all back in one command. The Makefile is where the
# package comes from, so that is what this checks -- a default `make
# install` has no applications in it, `APPS=all` is the old behaviour,
# and `make install-apps` places them and nothing else.
#
# A staging install is copies of an already-built tree, so this costs
# about two tenths of a second; it is here rather than in a pty suite
# because what it tests is the *packaging*, which no running desktop can
# see.
#
# **It covers every route, and that is why `APPS ?= none` is a default
# rather than a flag.** `packaging/deb/build-deb.sh` passes `APPS=none`
# explicitly, but the Homebrew formula is `make install PREFIX=#{prefix}`
# and the AUR `PKGBUILD` is `make ... PREFIX=/usr DESTDIR="$pkgdir"
# install` -- neither says anything about APPS, so both get whatever the
# default is. Had the bare install been a flag the packager remembers,
# two of the three routes would have gone on shipping applications and
# nothing here would have noticed. The first check below is the guard
# for all three.
#
# **PREFIX is left alone on purpose.** The module directory is compiled
# into the shell, so the Makefile forces a rebuild whenever PREFIX or
# MODDIR changes (`$(B)/.moddir`) -- and the first version of this test
# said `PREFIX=/usr`, which quietly rebuilt `build/hibr` with
# HIBR_MODDIR=/usr/lib/hibr and **left it that way**. Every test after it
# in the same run was then using a shell that searched the installed
# module directory: `691-usermods` saw a bare `csv` autoload, which reads
# as ADR 0044 being broken and was this test poisoning the tree.
# `packaging/deb/build-deb.sh` does change PREFIX, and puts the tree back
# with `trap 'make' EXIT` -- it already knew. What is under test here is
# APPS, which has nothing to do with where the thing would be installed.
d=${TMPDIR:-/tmp}/hibr-bare-$$
rm -rf "$d"

make -s install DESTDIR="$d/bare" > /dev/null 2>&1
echo "the default install has no applications: $(find "$d/bare" -path '*/desktop/apps/*' -name '*.hibr' | wc -l)"
echo "and still has the window manager: $(find "$d/bare" -path '*/desktop/wm/*' -name '*.hibr' | wc -l | awk '$1 > 10 { print "yes"; exit } { print "no" }')"
echo "and the desktop's own apps: $(find "$d/bare" -path '*/desktop/system/*' -name '*.hibr' | wc -l | awk '$1 > 5 { print "yes"; exit } { print "no" }')"
echo "and the Control Panel's panes: $(find "$d/bare" -path '*/desktop/control-panel/*' -name '*.hibr' | wc -l | awk '$1 > 10 { print "yes"; exit } { print "no" }')"

make -s install-apps DESTDIR="$d/apps" > /dev/null 2>&1
echo "install-apps places them: $(find "$d/apps" -name '*.hibr' | wc -l | awk '$1 > 15 { print "yes"; exit } { print "no" }')"
echo "and nothing else: $(find "$d/apps" -name '*.hibr' -not -path '*/desktop/apps/*' | wc -l)"

# Both halves together are what `make install APPS=all` places, which is
# what one `make install` placed before 0.99.194: the split is a default,
# not a loss.
make -s install APPS=all DESTDIR="$d/full" > /dev/null 2>&1
a=$(cd "$d/full" && find . -name '*.hibr' | sort | md5sum)
make -s install-apps DESTDIR="$d/bare" > /dev/null 2>&1
b=$(cd "$d/bare" && find . -name '*.hibr' | sort | md5sum)
[ "$a" = "$b" ] && echo "bare plus apps is the whole desktop" ||
	echo "bare plus apps DIFFERS from APPS=all"

rm -rf "$d"
