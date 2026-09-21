x=aabbaa
echo "anchor prefix: ${x/#aa/X}"
echo "anchor suffix: ${x/%aa/X}"
echo "anchor miss: ${x/#bb/X} ${x/%bb/X}"
echo "anchor empty: ${x/#/P} ${x/%/S}"
y=abc
echo "anchor glob: ${y/#a*/X} ${y/%*c/X}"
p=/usr/local/bin
echo "strip prefix: ${p/#\/usr/USR}"

ref=target
target=hit
echo "indirect: ${!ref}"
echo "indirect modifier: ${!ref:-fallback}"
echo "indirect strip: ${!ref#h}"
echo "indirect case: ${!ref^^}"
unset target
echo "indirect unset: [${!ref:-fallback}]"

zqa=1
zqb=2
zqz=3
echo "names star: ${!zq*}"
echo "names at: ${!zq@}"
echo "names none: [${!nosuchpfx*}]"

a=(p q r)
echo "negative: ${a[-1]} ${a[-2]} ${a[-3]}"
a[7]=z
echo "negative sparse: ${a[-1]} [${a[-2]}]"
b=(p q r)
unset b[-1]
echo "negative unset: ${b[*]}"
c=(p q r)
i=1
echo "still normal: ${c[0]} ${c[i]} ${c[1+1]} ${c[2]}"

q1=abc
q2="a b'c"
echo "transform Q: ${q1@Q} ${q2@Q}"
echo "transform U/L/u: ${q1@U} ${q1@L} ${q1@u}"
esc='a\tb'
printf 'transform E: %s|\n' "${esc@E}"

read -r -a rarr <<< "one two three"
echo "read -a: ${rarr[1]} count=${#rarr[@]}"
printf 'a:b' | { read -r -d: rd; echo "read -d: $rd"; }
printf 'abcdef' | { read -r -n3 rn; echo "read -n attached: $rn"; }
printf 'abcdef' | { read -r -n 3 rn2; echo "read -n separate: $rn2"; }
read -r r1 r2 <<< "one two three"
echo "read split: [$r1][$r2]"

printf 'printf q: %q %q %q\n' "a b" "" "a*b"
printf 'printf q plain: %q\n' "/usr/bin-x_y.z"
