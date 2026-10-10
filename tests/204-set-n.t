# `set -n` takes effect from the next *command*, not the next line, so
# nothing after it on the same line runs (Gitea #154). Compared against bash,
# which dash agrees with, so this is the strongest kind of guard here: both
# reference shells say the same thing and hibr was alone.
#
# Note what case 2 settles: once noexec is on, the `set +n` that would turn
# it off does not run either, so it is a one-way door for the rest of the
# input rather than a window.
one() {
  local out st
  out=$("$SH" -c "$1" 2> /dev/null); st=$?
  printf '%-44s -> [%s] status %d\n' "$2" "$(printf '%s' "$out" | tr '\n' '|')" "$st"
}

one 'set -n; echo X'                                  'same line'
one 'set -n; set +n; echo X'                          'set +n after it never runs'
one 'echo A; set -n; echo B'                          'what came before still ran'
one 'f() { set -n; echo A; }; f; echo B'              'from inside a function'
one 'set -n; for i in 1 2; do echo $i; done; echo ok' 'a compound after it'
one '( set -n; echo A ); echo B'                      'a subshell keeps it to itself'
one 'set -n; echo X; f() { :; }; echo Y'              'a definition after it'
one 'echo A
set -n
echo B'                                               'on its own line, as before'

# An eval, a source and a trap are nested texts, and a `set -n` inside one
# stops the caller's list too: only the outermost text goes on reading.
one 'eval "set -n; echo A"; echo B'                   'through an eval'
one 'set -n; eval "echo A"; echo B'                   'an eval after it'
one 'source /dev/stdin <<< "set -n"; echo B'          'through a source'

# Reading goes on, so a syntax error further down is still reported -- which
# is the whole point of noexec and is why this cannot be done by stopping the
# shell.
err=$("$SH" -c 'set -n
echo X
if' 2>&1 > /dev/null)
[ -n "$err" ] && echo "a later syntax error is still reported"

# The same through a pipe, which is a different read loop.
printf 'set -n\necho X\n' | "$SH"
echo "piped: nothing above this line"

# And the command-line form, which has always worked.
"$SH" -n -c 'echo X'
echo "hibr -n -c: nothing above this line, status $?"
