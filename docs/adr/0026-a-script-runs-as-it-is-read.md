# 0026 — A script runs as it is read; `checkfirst` parses it whole first

Status: accepted

## Context

Until 0.59 hibr read a whole script, parsed all of it, and only then ran
it. bash reads one complete command, runs it, and reads the next -- for a
script file, for `-c` text, for `source`, for `eval` and for standard input.
Three things differ between the two, each confirmed against bash:

- **A late syntax error.** bash runs every line before it and then stops
  with status 2; hibr ran none of the script. This was the largest known
  difference left in `docs/backlog.md`'s "Stay a drop-in for bash".
- **A script arriving on a pipe.** `(echo 'echo a'; sleep 2; echo 'echo
  b') | hibr` printed `a` after two seconds, where bash prints it at once --
  hibr waited for the end of its input before running anything.
- **A `read` in a script on standard input** takes the script's own next
  line in bash (POSIX requires the input to be left just past the command
  read). hibr had already swallowed the rest, and `read` saw end of file.

Parsing everything first is also worth having: a script that does not parse
runs none of its side effects, which is the safer failure for a program that
wrote the script and cannot watch it run.

## Decision

A script runs as it is read, by default, everywhere bash does it:
`hibr_run` parses one complete command line (`p_line`), runs it, releases its
parse memory unless it defined a function, and parses the next, with one
lexer so line numbers carry on. Standard input that is not a terminal is
read a line at a time, a byte at a time from a pipe and a chunk at a time
from a file, which is seeked back to just past what was read -- so a `read`
in the script sees the next line, as in bash.

`checkfirst` -- `hibr --checkfirst`, `set -o checkfirst`, `shopt -s
checkfirst` -- parses the whole text first and runs none of it if it does not
parse: a script file, `-c` text, standard input (then read whole) and a
`source`d file. Agent mode turns it on. `eval`, traps and command
substitutions always run as read: their text is built while the script runs,
and checking it first would parse every `$(…)` twice.

## Consequences

A late syntax error behaves as in bash, piped scripts stream, and a script
can carry its own input after a `read`. Nothing measurable was paid: the
`while` loop's instruction count moved by 0.001%, and sourcing the whole
desktop is 0.02% cheaper, since each command's parse memory is released as
it finishes instead of at the end.

One case cannot match bash. A command still incomplete after 64 lines --
a long function, piped in -- is re-parsed only when it has grown by a
quarter or nothing more is waiting, since re-parsing on every line made a
3,000-line function take three seconds. Lines already waiting in the pipe
past its end are read with it, and a pipe cannot give them back; so a `read`
in such a script, straight after such a command, misses the line it would
have had in bash. A file on standard input is seeked back and is exact.
`checkfirst` is a deliberate difference: bash has no such option.

---

[← decisions](README.md)
