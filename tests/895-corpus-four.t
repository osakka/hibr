# Four differences found by running real system scripts under both shells:
# $[ ] arithmetic, select at end of input and over nothing, and the status a
# set -e pipeline stops with when its last stage fails.
x=4
echo "[$[x-1]] [$[ 2 * 3 ]] [$[x]] $[1+1]"
a=(1 2); echo $[ a[1] + 1 ]
echo "a literal: \$[x]"
set --
select v; do echo never; break; done < /dev/null
echo "select over nothing: $?"
select v in a b; do echo "got $v"; break; done <<< 2
select v in a b; do :; done < /dev/null
echo "select at end of input: $?"
( set -e; false | (exit 2); echo never ); echo "errexit pipeline: $?"
( set -e; (exit 4) | (exit 3) ); echo "errexit pipeline, both fail: $?"
