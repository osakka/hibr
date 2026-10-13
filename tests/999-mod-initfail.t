# A module whose init loads a module of its own and then fails. m_load took
# this module's record out of s->mods by *position* -- `s->mods.n--` -- which
# names the right one only while nothing else was added in between, and an
# init that calls hibr_require adds one: hold asks for pty and then term, so
# in a module directory holding hold.so and pty.so but no term.so the shell
# dropped *pty's* record, left hold's -- freed, its object already unmapped
# -- in the list, and the next walk of that list dereferenced it. `need hold`
# there was a SIGSEGV in m_need, 3 runs out of 3 (Gitea #201).
#
# The module the init loads stays loaded on purpose: it initialised fine and
# is nobody's to unload.
d=/tmp/hibr-modfail-$$
mkdir -p "$d"
if (tcc -shared -fPIC -Iinclude -o "$d/modfail.so" tests/modfail.c ||
	gcc -shared -fPIC -Iinclude -o "$d/modfail.so" tests/modfail.c) 2> /dev/null
then
	export HIBR_MODPATH="$d:${HIBR_MODPATH:-./build/mods}"
	need modfail; echo "need rc=$?"
	echo "the display its init loaded: $(mod list | grep -c '^console ')"
	echo "the one whose init failed: $(mod list | grep -c '^modfail ')"
	mod list > /dev/null; echo "the list is still walkable"
	mod drop all > /dev/null; echo "and still droppable"
else
	echo "no compiler, skipped"
fi
rm -rf "$d"
