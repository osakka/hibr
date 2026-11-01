# Extended patterns in pathname expansion (Gitea #422). They were always on
# for case and [[ ]], but a word whose only pattern was @( !( or +( never
# reached globbing: gmeta and w_meta knew * ? [ alone, so `echo q@(1|2)`
# printed itself while `echo q?(1)` globbed. Compared against bash, which
# needs the shopt; hibr takes it as a no-op, since its patterns are always on.
shopt -s extglob

d=$(mktemp -d) || exit 1
trap 'rm -rf "$d"' EXIT
cd "$d" || exit 1
touch q1 q2 q3 a.sh b.sh .hid .h2 'x@(1|2)'
mkdir sub1 sub2
touch sub1/f.c sub2/g.c

echo q@(1|2)
echo !(a.sh)
echo !(a.sh|b.sh)
echo q+(1|2)
echo q?(1) q*(1) q+([0-9])
echo q@(1|2)x
echo zz@(1|2)
echo sub@(1|2)/*.c
echo @(sub1|sub2)/!(f.c)

for f in q@(1|3); do echo "for $f"; done
a=(q@(1|2))
echo "${#a[@]} ${a[*]}"
echo "$(echo q+(2|3))"

x='q@(1|2)'
echo $x
echo "$x"
y=q
echo $y@(2|3)

echo "q@(1|2)" 'q@(1|2)' q\@\(1\|2\)
echo *"@(1|2)"
echo ["!"q]*

echo !(.hid)
echo @(.hid|q1)
echo @(q1|.h*) @(.*)
echo ?(.hid) *(.hid) +(.hid)
echo @(*) @(*|.x) @(?hid)
echo .@(hid) .!(hid)
echo ?(x).hid *(x).hid !(x).hid +(x).hid @(x|).hid

echo @ ! a@b a!b a+b
[[ ! q1 == q2 ]] && echo not
r=y
echo ${!r}
case q2 in q@(1|2)) echo case ;; esac
[[ q3 == q+([0-9]) ]] && echo cond

(shopt -s nullglob; echo zz@(1|2) end)
set -f
echo q@(1|2) !(a.sh)
set +f
echo q@(1|2)
