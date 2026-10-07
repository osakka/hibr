# := takes a command's result whatever kind of command it is: a builtin's
# result slot, a function's ret, and -- since 0.99.89 -- a program's standard
# output, the way $( ) does. Before that a program bound nothing and said it
# had succeeded, which is what made `v := curl ...` print its body and leave v
# empty (Gitea #122). Recorded: bash has no :=.
#
# What must *not* change is a := that is not a plain foreground command: a
# pipeline stage and a background job each run in a child, so the binding has
# always happened in a process that is about to go, and the parent's variable
# stays empty. Those two are here as the guard on that.
v := str upper hello
echo "builtin: [$v] status=$?"
f() { ret "from a function"; }
v := f
echo "function: [$v] status=$?"
v := /bin/echo a program
echo "program: [$v] status=$?"
v := /bin/sh -c 'printf "one\ntwo\n\n\n"'
echo "trailing newlines stripped: [$v]"
v := /bin/false
echo "a program that failed: [$v] status=$?"
v := /bin/sh -c 'echo out; exit 3'
echo "failed with output: [$v] status=$?"
v := /bin/echo redirected > /dev/null
echo "its own redirection wins: [$v] status=$?"
v=unset
v := /bin/echo piped | cat
echo "a pipeline stage binds in its own child: [$v]"
v=unset
v := /bin/echo backgrounded &
wait
echo "so does a background job: [$v]"
n := /bin/sh -c 'i=0; while [ $i -lt 4096 ]; do printf "%s\n" 0123456789abcdefghijklmnopqrstuvwxyz; i=$((i + 1)); done'
echo "more than a pipe buffer: ${#n} bytes"
