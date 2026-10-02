# Assignments in one command are made left to right, each seeing the one
# before, as bash makes them; the command's own words are expanded first.
x=1 y=$x; echo "y=[$y]"
a=5:6 a=${a%:*}; echo "a=[$a]"
p=/usr/local/bin d=${p%/*} b=${p##*/}; echo "$d $b"
unset x; x=1 y=$x printenv y; echo "after x=[${x-unset}]"
unset x; x=1 echo "[${x-unset}]"
f() { echo "f sees ${q:-none} ${r:-none}"; }; q=7 r=$q f; echo "q after=[${q-unset}]"
n=0; n=$((n + 1)) m=$((n * 10)); echo "n=$n m=$m"
i=3 i=$((i + 1)) i=$((i * 2)); echo "i=$i"
