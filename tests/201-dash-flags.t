# $- is the letters of the short options this shell has on, then how it was
# started: i interactive, c for -c, s for commands on standard input.
#
# Recorded rather than compared because the letters are hibr's own. bash's
# include h (hashall) and B (braceexpand), which hibr has no option for and
# therefore cannot honestly report; a script asking the usual questions --
# `case $- in *i*)`, `*e*` -- gets the same answer from both, and that is
# what tests/199-startup-opts.t checks against bash (Gitea #153).
#
# $SH is this shell, so the lines below can ask about a shell started some
# other way than this one was.

echo "a script of its own:  [$-]"
set -e; echo "with errexit:         [$-]"
set -u; echo "and nounset:          [$-]"
set +e; echo "errexit off again:    [$-]"
set -C -H; echo "noclobber, histexpand:[$-]"
set +u +C +H; echo "back to none:         [$-]"

# The order is the table's, not the order they were asked in.
set -e -C -u; echo "asked e C u:          [$-]"
set +e +C +u
set -u -C -e; echo "asked u C e:          [$-]"
set +e +C +u

# How this shell was started is the last letters.
"$SH" -c 'echo "a -c shell:           [$-]"'
"$SH" -ec 'echo "-c with errexit:      [$-]"'
echo 'echo "commands on stdin:    [$-]"' | "$SH"
echo 'echo "-s with arguments:    [$-] $1"' | "$SH" -s one
"$SH" --agent -c 'echo "--agent brings its own:[$-]"'

# A script named on the line is neither -c nor -s.
d=/tmp/hibr-dash-$$
printf 'echo "a named script:      [$-]"\n' > "$d"
"$SH" "$d"
"$SH" -x "$d" 2>/dev/null
rm -f "$d"
