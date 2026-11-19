# A redirection the shell applies itself -- on a function call, a group, an
# `exec` -- saves the descriptor it replaces so it can put it back, and that
# copy is the shell's alone: no program run inside sees it. hibr's copy was
# a plain dup, inheritable at the lowest free number, so every program run
# in a function or group with a redirection held what the redirection had
# replaced -- an app process started from the desktop held its terminal.
# Compared against bash, which keeps its copies above 9 and closed on exec.

fds() { ls /proc/self/fd | sort -n | tr '\n' ' '; echo; }
f() { fds; }

echo "a function with input redirected:"
f < /dev/null
echo "a group with output redirected:"
{ fds; } > /tmp/hibr-redir-saved-$$; cat /tmp/hibr-redir-saved-$$
echo "exec in a subshell with input redirected:"
( exec ls /proc/self/fd < /dev/null ) | sort -n | tr '\n' ' '; echo
echo "a function calling one with its own redirection:"
g() { f 2> /dev/null; }
g < /dev/null
echo "a descriptor the script opened is still inherited where asked for:"
exec 3< /dev/null
f < /dev/null
exec 3<&-
rm -f /tmp/hibr-redir-saved-$$
