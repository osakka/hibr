# The core headers bring in what they name. glibc's <sys/types.h> pulls
# <sys/select.h> and with it sigset_t, so pri.h declaring jc_blk and
# jc_sleep with one compiled here for releases while macOS, whose
# <sys/types.h> does not, could not build hibr at all: Homebrew stopped at
# 0.99.425. Strict POSIX takes glibc's extras away, which is the Mac's
# position, so a header leaning on an accident fails here first. The
# Mac-only branch of the containment module is checked the same way.
ok="the headers bring in every type they name"
command -v gcc > /dev/null 2>&1 || { echo "$ok"; exit 0; }
bad=0
for h in hibr.h pri.h; do
	printf '#include "%s"\n' "$h" |
		gcc -std=c99 -D_POSIX_C_SOURCE=200809L -fsyntax-only -Iinclude -x c - \
			2> /dev/null || { echo "include/$h leans on a type it does not include"; bad=1; }
done
gcc -fsyntax-only -U__linux__ -D__APPLE__ -Iinclude mods/contain.c 2>&1 |
	grep -q "unknown type name" && { echo "mods/contain.c's Mac branch names a type it does not include"; bad=1; }
echo "$ok"
exit 0
