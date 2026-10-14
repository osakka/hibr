# A module a person installed lives in $XDG_DATA_HOME/hibr/mods, which a
# non-root shell searches when something asks for a module **by name** --
# and which the autoload index a missing command builds leaves out
# (ADR 0044). Building that index dlopens every candidate to read its
# descriptor, so a typo must never reach a folder anybody can write to.
d=${TMPDIR:-/tmp}/hibr-usermods-$$
mkdir -p "$d/hibr/mods"
cp ./build/mods/csv.so "$d/hibr/mods/"
export XDG_DATA_HOME=$d
unset HIBR_MODPATH

echo "avail lists it: $(mod avail | awk -F'\t' -v d="$d" '$5 ~ d { n++ } END { print n + 0 }')"
mod load csv > /dev/null && echo "mod load by name: ok"
echo "loaded: $(mod list | grep -c '^csv ')"
mod drop csv > /dev/null
echo "dropped: $(mod list | grep -c '^csv ')"

# `need` asks for an interface and walks the same path.
need csv && echo "need by interface: ok"
mod drop csv > /dev/null

# The asymmetry, and the whole point: a bare `csv` is a command nothing
# provides, so the shell builds the index -- which has not looked in
# there. The same module on HIBR_MODPATH does autoload, which is what
# says this is the folder's doing and not the module's.
csv 2>&1 | head -1
HIBR_MODPATH=$d/hibr/mods $HIBR -c 'csv' 2>&1 | head -1

# Asked for by name first, it is there.
need csv > /dev/null 2>&1
csv 2>&1 | head -1

rm -rf "$d"
