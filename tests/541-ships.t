# What `make install` places of the desktop is what packaging/ships lists,
# and the parts those bring -- nothing more and nothing less (Gitea #271,
# ADR 0047). Until 0.99.230 the install copied the desktop folder whole
# and took out only apps/, so eight panes of applications it did not ship,
# a strip module and every theme and colour scheme went with it.
#
# tools/ships.hibr is the one place that decides, and this holds the
# Makefile to it: the default install against the tool's list, file for
# file; the extras against the tool's other list; and the two together
# against SHIP=all, so nothing falls between them. `-c` first, which
# refuses a list naming a file that is not there, a part listed on its
# own, a shipped theme whose colours do not ship, and a pane filed under
# Applications that no application brings, or the other way round.
#
# A staging install is copies of an already-built tree, so this costs
# well under a second; it is here rather than in a pty suite because what
# it tests is the *packaging*, which no running desktop can see. Every
# route installs this way -- build-deb.sh, the Homebrew formula and the
# AUR's PKGBUILD are each a plain `make install` -- so the default is the
# guard for all three.
#
# **PREFIX is left alone on purpose.** The module directory is compiled
# into the shell, so the Makefile forces a rebuild whenever PREFIX or
# MODDIR changes -- and an earlier version of this test said
# `PREFIX=/usr`, which quietly rebuilt `build/hibr` with
# HIBR_MODDIR=/usr/lib/hibr and left it that way for every test after it.
d=${TMPDIR:-/tmp}/hibr-ships-$$
rm -rf "$d"
kinds="apps system control-panel control-strip menubar savers themes colours lang bundles"

"$HIBR" tools/ships.hibr -c > /dev/null && echo "the list and the parts check out"

make -s install DESTDIR="$d/def" > /dev/null 2>&1
desk=$(find "$d/def" -type d -path '*/share/hibr/desktop' | head -1)
got=$(cd "$desk" && find $kinds -type f 2> /dev/null | sort)
want=$("$HIBR" tools/ships.hibr)
[ "$got" = "$want" ] && echo "the default install is the list and its parts, file for file" ||
	diff <(echo "$want") <(echo "$got")
echo "and still has the window manager: $(find "$desk/wm" -name '*.hibr' | wc -l | awk '$1 > 10 { print "yes"; exit } { print "no" }')"
for f in apps/files.hibr apps/Accessories/calc.hibr control-panel/filesview.hibr \
	 themes/classic.json colours/paper.json savers/mystify.hibr savers/saver.hibr \
	 lang/strings.txt; do
	[ -f "$desk/$f" ] && echo "ships $f"
done
for f in apps/Internet/youtube.hibr control-panel/tube.hibr control-strip/nextprayer.hibr \
	 themes/meadow.json themes/meadow.png colours/dracula.json savers/matrix.hibr \
	 lang/ar.json; do
	[ -f "$desk/$f" ] || echo "leaves out $f"
done
echo "no empty folder is left behind: $(find "$desk/apps" -type d -empty | wc -l)"

make -s install-extras DESTDIR="$d/ext" > /dev/null 2>&1
edesk=$(find "$d/ext" -type d -path '*/share/hibr/desktop' | head -1)
got=$(cd "$edesk" && find . -type f | sed 's|^\./||' | sort)
[ "$got" = "$("$HIBR" tools/ships.hibr -n)" ] && echo "install-extras places exactly the rest"

# Both halves together are what SHIP=all places.
make -s install SHIP=all DESTDIR="$d/full" > /dev/null 2>&1
a=$(cd "$d/full" && find . -type f | sort | xargs md5sum)
make -s install-extras DESTDIR="$d/def" > /dev/null 2>&1
b=$(cd "$d/def" && find . -type f | sort | xargs md5sum)
[ "$a" = "$b" ] && echo "the default plus the extras is the whole desktop" ||
	echo "the default plus the extras DIFFERS from SHIP=all"

rm -rf "$d"
