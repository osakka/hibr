d=/tmp/hibr-strict-test
rm -rf $d; mkdir -p $d; cd $d; touch a.c b.c
fn count(...rest) { echo "${#rest[@]}"; }
f="my file.txt"
g="*.c"
list=$(printf "a b\nc")
empty=""
echo "--- default:"
count $f
count $g
count $list
count $empty
# Under -S an expansion never splits and never globs, so the first three give
# one argument each. An empty one still gives none, as zsh does and as the
# default does -- that is the one place -S used to be stricter than either.
echo "--- strict:"
set -S
count $f
count $g
count $list
count $empty
count *.c
a=(one "two words" three)
count ${a[@]}
set -- p "q r"
count $@
count "$f"
set +S
count $f
cd /; rm -rf $d
