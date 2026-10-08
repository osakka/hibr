# mod avail lists what could be loaded; mod load takes a path or a bare name.
# Everything here is scoped to ./build/mods so the machine's own installed
# modules cannot change the result.
export HIBR_MODPATH=./build/mods

# In a pipe it is fields, not a table: name, version, ABI, state, path, the
# interface it offers, its builtins, its description. No header, the same as
# mod list, so a script reads it with no line to skip.
mod avail | awk -F'\t' 'NR == 1 { print "fields " NF }'

one() { mod avail | awk -F'\t' -v m="$1" '$1 == m { print $1 " abi N " $4 }'; }

echo "--- before loading"
one sys

echo "--- loaded from the file avail found"
mod load sys > /dev/null && one sys
upper "module still works"

echo "--- dropped again"
mod drop sys > /dev/null && one sys

echo "--- loaded from a path outside the search path: not that file"
mod load "$PWD/build/mods/sys.so" > /dev/null && one sys
upper "still works by path"
mod drop sys > /dev/null

echo "--- the suffix is optional in a path"
mod load ./build/mods/sys && upper "no suffix needed"
mod drop sys > /dev/null

echo "--- errors"
mod load ./nosuchfile.so 2>/dev/null; echo "missing file rc=$?"
mod drop nosuchmodule 2>/dev/null; echo "missing name rc=$?"
echo "absent count $(mod avail | awk -F'\t' '$1 == "nosuchmodule" { n++ } END { print n + 0 }')"
