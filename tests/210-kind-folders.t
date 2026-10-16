# The catalogue and the desktop both know where a kind lives, and they
# cannot be made to know it once (Gitea #255).
#
# The desktop declares a scan list per kind -- DT_APPDIRS, CP_PANEDIRS,
# CS_MODDIRS, SV_DIRS and the theme, colour and language ones -- each in
# the file that owns that kind, and each appends roots of its own at
# session time, so a shared table could only ever describe the four
# standard scopes and every list would still have to add to it. The
# catalogue is installed as its own command and must run with no desktop
# present, so it cannot read the desktop's declarations at all.
#
# So the duplication stays, and what closes it is this: a kind the
# catalogue offers must be a folder the desktop actually reads. The two
# agree today only because every folder happens to be named after its
# kind -- a coincidence of naming, not a construction -- and the day a
# tenth kind is added to one side alone, or a folder stops matching its
# kind, this fails rather than shipping a catalogue that can install
# something nothing will ever load.
#
# `mods` is deliberately exempt: a module is found on the module search
# path, which is the shell's and not a scan list of the desktop's.

cd "${0%/*}/.." || exit 1
read -ra kinds <<< "$(sed -n '/^AP_KINDS=(/,/)/p' programs/catalogue.hibr | tr -d '()' | sed 's/AP_KINDS=//')"
bad=0
n=0
for k in "${kinds[@]}"; do
	[ -n "$k" ] || continue
	[ "$k" = mods ] && continue
	n=$((n + 1))
	if ! grep -rq "hibr/$k\"\|hibr/$k/" programs/desktop/; then
		echo "the catalogue offers $k and no desktop scan list reads hibr/$k"
		bad=$((bad + 1))
	fi
done
[ "$n" -gt 6 ] || echo "only $n kinds checked, which is too few to be reading the list"
[ "$bad" = 0 ] && echo "every kind the catalogue offers is a folder the desktop reads"
