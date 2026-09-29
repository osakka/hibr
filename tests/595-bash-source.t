# BASH_SOURCE is the file the running code came from: the script, a file
# being sourced, or -- inside a function -- the file the function was
# defined in, wherever it is called from. What lets a sourced file find its
# own directory, which is how the desktop finds its parts. And [0] of any
# computed name (RANDOM, SECONDS, BASH_SOURCE) is its value, as for any
# scalar in bash.
d=/tmp/hibr-bsrc-$$
mkdir -p "$d/lib"
cd "$d" || exit 1
printf '%s\n' 'echo "in a: $BASH_SOURCE"' '. ./lib/b.sh' \
	'echo "back in a: $BASH_SOURCE"' \
	'afn() { echo "afn: $BASH_SOURCE"; bfn; }' > lib/a.sh
printf '%s\n' 'echo "in b: ${BASH_SOURCE[0]}"' \
	'bfn() { echo "bfn: $BASH_SOURCE"; }' > lib/b.sh
[ -n "$BASH_SOURCE" ] && echo "the script itself has one"
. ./lib/a.sh
afn
echo "and a sourced file's directory: ${BASH_SOURCE%/*}" | grep -q . &&
	echo "a directory can be taken from it"
echo "${RANDOM[0]:+computed [0] reads the value}"
echo "[${nosuch[0]}] [${RANDOM[1]}]"
cd / && rm -rf "$d"
