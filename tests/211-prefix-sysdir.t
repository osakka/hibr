# A prefixed install creates nothing outside its own prefix.
#
# Every path in the Makefile derives from PREFIX except one: SYSDIR was
# written out as /var/lib/hibr when the system scope arrived in
# 0.99.197, and `make install` creates it. So `make install
# PREFIX=/opt/homebrew`, which is what the Homebrew formula runs, tried
# `install -d /var/lib/hibr` -- root-owned on a Mac and none of
# Homebrew's business -- and every `brew install hibr` failed with
# permission denied. A hibr bug, not a Homebrew one.
#
# /usr keeps the FHS path, because that is where a distribution puts
# state a program maintains and it is what the deb builds with.
# /usr/local does **not**, deliberately: Homebrew's own prefix on an
# Intel Mac is exactly /usr/local, so treating it as a system prefix
# would fix Apple Silicon and leave Intel broken the same way.
#
# A dry run rather than a real install: `make -n` prints the commands
# without running them, so this costs a fraction of a second and
# touches nothing.

cd "${0%/*}/.." || exit 1

# What the Homebrew formula really passes (Gitea #268): its `prefix` is the
# versioned keg, so a system scope derived from it would be inside the keg
# and gone at the next `brew upgrade` and `brew cleanup`. The formula names
# Homebrew's own var/ instead and says MKSYSDIR=no, so the install makes no
# folder outside the keg and the catalogue makes it on first use.
n=$(make -s -n PREFIX=/opt/homebrew/Cellar/hibr/9 SYSDIR=/opt/homebrew/var/lib/hibr \
	MKSYSDIR=no DESTDIR=/tmp/hibr-prefix-check install 2>/dev/null |
	grep -c 'install -d -m 755 /tmp/hibr-prefix-check[^ ]*var/lib/hibr')
echo "the formula's install makes no system folder: $n"
for p in /usr /usr/local /opt/homebrew; do
	out=$(make -s -n PREFIX="$p" DESTDIR=/tmp/hibr-prefix-check install 2>/dev/null |
		grep -o 'install -d -m 755 /tmp/hibr-prefix-check[^ ]*var/lib/hibr' | head -1)
	d=${out##* }
	d=${d#/tmp/hibr-prefix-check}
	printf '%-14s %s\n' "$p" "$d"
	case $p in
	/usr) [ "$d" = /var/lib/hibr ] ||
		echo "  the FHS prefix must keep /var/lib/hibr" ;;
	*) case $d in
	   "$p"/*) ;;
	   *) echo "  $p creates $d, which is outside its own prefix" ;;
	   esac ;;
	esac
done
