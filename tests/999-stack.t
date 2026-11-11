# Every module hibr builds says its stack is not executable (ADR 0055).
# tcc's own linker writes no PT_GNU_STACK, glibc reads its absence as an
# executable stack -- so a thread a module starts gets an rwx one -- and
# glibc 2.41 refuses to load such a module into a process whose own stack
# is not executable: the sanitizer shell, a distribution's gcc-built hibr,
# or anything else that loads a hibr module. tools/tccld links them with
# the system linker; a build without one falls back to tcc and fails here,
# so no packaging ships the old kind unnoticed.
command -v readelf > /dev/null 2>&1 || { echo "every module's stack is not executable"; exit 0; }
[ "$(uname -s)" = Linux ] || { echo "every module's stack is not executable"; exit 0; }
for m in ./build/mods/*.so; do
	[ -f "$m" ] || continue
	f=$(readelf -lW "$m" 2>/dev/null | awk '$1 == "GNU_STACK" { print $7 }')
	[ "$f" = RW ] || echo "$m: GNU_STACK ${f:-missing}"
done
echo "every module's stack is not executable"
