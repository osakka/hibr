# {name} redirections that copy a descriptor: the shell picks a number of
# ten or more, puts it in the variable, and {name}>&- closes that one --
# never the redirection's own default, which is stdout.

exec {fd}>&2
[ "$fd" -ge 10 ] && echo "a descriptor was chosen"
echo "to the copy" >&$fd 2>/dev/null
exec {fd}>&-
echo "stdout is still open"

exec {out}>&1
echo "through the copy" >&$out
exec {out}>&-

exec {keep}>&2 2>/dev/null
echo "stderr is set aside" >&2
exec 2>&$keep {keep}>&-
echo "and back"

exec 2>/dev/null
exec 3>&$nothing
echo "an empty descriptor fails: $?"
exec {nothing}>&-
echo "closing an empty one fails: $?"
echo "and stdout survives both"
