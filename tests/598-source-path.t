# . and source look a bare name up on PATH -- a readable file, executable or
# not -- and fall back to the current directory; a name with a slash in it
# is only ever the path it names.
d=/tmp/hibr-srcpath-$$
mkdir -p "$d/bin" "$d/cwd"
printf 'echo "lib from PATH, ${BASH_SOURCE##*/}, ${BASH_SOURCE:0:1}"\n' > "$d/bin/mylib"
printf 'echo "lib from cwd, ${BASH_SOURCE}"\n' > "$d/cwd/mylib"
printf 'echo "only here, ${BASH_SOURCE}"\n' > "$d/cwd/other"
mkdir "$d/bin/adir"
cd "$d/cwd"
PATH="$d/bin:$PATH"
. mylib
. other
. ./mylib
source mylib one two
. adir 2>/dev/null; echo "a directory is not sourced: $?"
. nothere 2>/dev/null; echo "missing: $?"
cd /
rm -rf "$d"
