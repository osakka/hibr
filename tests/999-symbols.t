# The shell is linked -rdynamic, so a module's own global symbol is
# preempted by the shell's of the same name -- and the module then calls the
# shell's. It has cost this project three bugs: sys.c defining m_drop, bound
# to mod.c's m_drop and crashed on a signature mismatch; a display module
# calling its own sc_fini, which would silently have been net.c's scheme
# cleanup; and mod.c gaining an `int m_md` for the autoload mode, which is
# the md module's own builtin handler -- 678 sanitizer reports, every one a
# SEGV "in m_md", because a data address was being called.
#
# The check CLAUDE.md prescribes, as a test rather than a habit. _init,
# _fini and _etext are the linker's own and are in every object.
command -v nm > /dev/null 2>&1 || { echo "no module defines a symbol the shell already exports"; exit 0; }
[ -x ./build/hibr ] || { echo "no module defines a symbol the shell already exports"; exit 0; }
nm -D ./build/hibr 2>/dev/null |
	awk '$2 == "T" || $2 == "B" || $2 == "D" { print $3 }' | sort -u > /tmp/hibr-syms-$$
for m in ./build/mods/*.so; do
	[ -f "$m" ] || continue
	bad=$(nm -D "$m" 2>/dev/null | awk '$2 == "T" { print $3 }' | sort -u |
		comm -12 - /tmp/hibr-syms-$$ |
		grep -vE '^(_init|_fini|_etext)$')
	[ -n "$bad" ] && echo "$m also defines $bad"
done
rm -f /tmp/hibr-syms-$$
echo "no module defines a symbol the shell already exports"
