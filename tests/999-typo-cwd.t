# A mistyped command does not load objects from the current directory
# (Gitea #431): building the autoload index dlopened every .so there, which
# runs its constructors -- a typo in /tmp or a cloned repository ran them.
d=$(mktemp -d)
cd "$d"
if command -v cc > /dev/null && printf '#include <fcntl.h>\n__attribute__((constructor)) static void f(void) { creat("RAN", 0600); }\n' > c.c &&
   cc -shared -fPIC -o evil.so c.c 2> /dev/null; then
	nosuchcommand_zz 2> /dev/null
	[ -e RAN ] && echo "constructor ran" || echo "not loaded"
else
	echo "not loaded"
fi
cd /
rm -rf "$d"
