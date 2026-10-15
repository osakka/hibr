# OSTYPE, HOSTTYPE and MACHTYPE, which hibr set none of until 0.99.195
# (Gitea #234). Compared against bash rather than recorded, because the
# whole point is agreeing with it -- a recording would freeze one
# machine's answer and pass on every other by not being run there.
#
# What is printed is split in two on purpose. The invariants hold on
# every platform and are what a script actually relies on; the exact
# triple is printed only on Linux, where hibr can reproduce bash's
# configure-time spelling (`linux-gnu`) from `uname()`. On a Mac bash
# says whatever its build machine's triple was (`darwin23.0`) and hibr
# says `darwin` plus the running kernel's major, which is honest and is
# not identical -- so asserting it everywhere would be a test that can
# only pass where it was written.

# Every platform: the machine is what uname says, the triple is built
# from it, and the system is lowercase and not empty.
m=$(uname -m)
[ "$HOSTTYPE" = "$m" ] && echo "HOSTTYPE is the machine"
[ -n "$OSTYPE" ] && echo "OSTYPE is set"
case $OSTYPE in *[A-Z]*) echo "OSTYPE has capitals" ;; *) echo "OSTYPE is lowercase" ;; esac
case $MACHTYPE in "$HOSTTYPE"-*) echo "MACHTYPE opens with the machine" ;; *) echo "MACHTYPE does not" ;; esac

# Linux: the whole triple, exactly as bash spells it.
if [ "$(uname -s)" = Linux ]; then
	echo "$OSTYPE"
	echo "$MACHTYPE"
fi

# They are ordinary variables once set, as in bash: a script may change
# one, and an assignment is not refused or recomputed.
OSTYPE=something-else
echo "$OSTYPE"
