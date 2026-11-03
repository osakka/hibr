# Review of the performance work (Gitea #451). A one-line $( ) or ( ) of
# one command runs its program in place of the subshell -- except where the
# subshell has more to do than run it:
#
# an incomplete line is a syntax error, reported with status 2, where the
# fast path said nothing and handed back the previous status;
b=$(if)
echo "if: $?"
false
c=$(while true)
echo "while: $?"
f=$(echo a |)
echo "pipe: $?"
# and x := prog needs the pipe the shell makes to capture what it prints,
# so run in place it printed instead of binding.
( x := /bin/echo sub-bound-alone )
y=$( x := /bin/echo cap-bound )
echo "y=[$y]"
