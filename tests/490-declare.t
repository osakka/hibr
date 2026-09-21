declare -i n
n=3+4
echo "integer: $n"
n=10/3
echo "integer div: $n"

declare x=plain
echo "plain: $x"

f() { declare loc=inside; echo "in function: $loc"; }
loc=outside
f
echo "after function: $loc"

g() { declare -g glob=made; }
g
echo "global: $glob"

declare -n ref=real
real=value
echo "nameref read: $ref"
ref=written
echo "nameref write: $real"
h() { declare -n p=$1; p=via-ref; }
h target
echo "nameref arg: $target"
declare -n a2=b2
declare -n b2=c2
c2=chained
echo "nameref chain: $a2"

export XPORT=1
export -p | grep -c 'XPORT'

declare -p n
declare -p ref
