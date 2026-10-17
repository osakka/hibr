# A module an administrator installed for every user of the machine lives
# in $HIBR_SYSDIR/mods, which every non-root shell searches between the
# person's own folder and the package's (ADR 0045). The order is what
# makes it useful: your copy still wins, and the package's is still the
# floor nothing can take away.
#
# Two things it deliberately is **not**. It is left out of the autoload
# index, like the user's own folder and for a stronger reason -- an
# administrator installing a module for everybody should not thereby
# change what a mistyped command does on that machine (ADR 0044). And
# the compiled-in default is checked before it is used at all: **the
# default is checked and a path you chose is yours**, so $HIBR_SYSDIR
# here is a path chosen by the person running it and is trusted the way
# $HIBR_MODPATH already is. The check itself is at the end.
d=${TMPDIR:-/tmp}/hibr-sysmods-$$
mkdir -p "$d/mods" "$d/empty"
cp ./build/mods/csv.so "$d/mods/"
export HIBR_SYSDIR=$d
export HIBR_MODPATH=$d/empty
unset XDG_DATA_HOME

echo "avail lists it: $(mod avail | awk -F'\t' -v d="$d/mods" '$5 ~ d { n++ } END { print n + 0 }')"
mod load csv > /dev/null && echo "mod load by name: ok"
mod drop csv > /dev/null
need csv && echo "need by interface: ok"
mod drop csv > /dev/null

# The same asymmetry the user's own folder has: a bare `csv` is a command
# nothing provides, so the shell builds the autoload index -- which has
# not looked in here. On HIBR_MODPATH the very same file does autoload,
# which is what says this is the folder's doing and not the module's.
csv 2>&1 | head -1
HIBR_MODPATH=$d/mods $HIBR -c 'csv' 2>&1 | head -1

# And the shell says where it is, so a script needs no second copy of
# the path: `catalogue --system` reads exactly this.
echo "the shell names it: $([ "$HIBR_SYSDIR" = "$d" ] && echo yes)"

# The compiled-in default must be as trusted as the shell itself: owned
# by root or by whoever owns the hibr binary, and writable by nobody else
# (Gitea #268). Root-only made every module a Homebrew install put there
# a file nothing loaded, since a Homebrew prefix -- binary and var/ alike
# -- is its user's. A shell built here with the default pointing at a
# folder this account owns is exactly that case, and tcc builds one in a
# fraction of a second; B= keeps it out of build/, so the tree's own shell
# is never rebuilt with somebody else's path in it.
make -s B="$d/b" SYSDIR="$d/sys" "$d/b/hibr" > /dev/null 2>&1
mkdir -p "$d/sys/mods"
cp ./build/mods/csv.so "$d/sys/mods/"
chmod 755 "$d/sys" "$d/sys/mods"
unset HIBR_SYSDIR
"$d/b/hibr" -c 'mod load csv > /dev/null 2>&1' && echo "a default owned by the binary's owner is searched: yes"
chmod 775 "$d/sys/mods"
"$d/b/hibr" -c 'mod load csv > /dev/null 2>&1' || echo "and one others may write is not: refused"
rm -rf "$d"
