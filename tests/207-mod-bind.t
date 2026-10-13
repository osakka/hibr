# A builtin that answers something fills the result slot and goes quiet while
# one is bound. `mod list` and `mod avail` did neither: `r := mod list` left r
# empty with status 0 and wrote the table to standard output, which in a
# full-screen program is into the middle of the screen, under the console's own
# drawing (Gitea #205). src/mod.c mentioned `bind` nowhere, where every one of
# the thirty module files honours it -- so the shell's own `mod` was the only
# thing that did not.
#
# Both halves are read for each verb separately: one verb binding correctly
# says nothing about the other, and a check that only read the variable would
# pass a version that still printed. Recorded: none of it is bash's.
export HIBR_MODPATH=./build/mods
mod load ./build/mods/sys.so

r := mod list
echo "list binds its own rows: $(printf '%s\n' "$r" | grep -c '^sys ')"
o=$(r := mod list; printf 'end\n')
echo "and prints nothing while bound: $(printf '%s' "$o" | grep -vc '^end$')"

a := mod avail
printf '%s\n' "$a" | awk -F'\t' '$1 == "sys" { print "avail binds the tab form: " $4 }'
o=$(a := mod avail; printf 'end\n')
echo "and prints nothing while bound: $(printf '%s' "$o" | grep -vc '^end$')"

# The plain form is what binds whatever stdout is, since the fitted one is
# laid out for a terminal nobody is reading there.
echo "unbound still prints: $(mod list | grep -c '^sys ')"
mod avail | awk -F'\t' '$1 == "sys" { print "unbound avail still prints: " $4 }'
