self=$(readlink /proc/$$/exe)
d=$(mktemp -d); cd "$d"
echo "-- test: integers as bash reads them"
for v in "" " " "+1" "-1" " 1 " "1x" "0x10" "010" "99999999999999999999" "-" "abc"; do
	[ "$v" -eq 1 ] 2>/dev/null; printf '[%s] -eq %s; ' "$v" "$?"
	[ 1 -lt "$v" ] 2>/dev/null; printf -- '-lt %s\n' "$?"
done
[ a == a ]; echo "== $?"
[ a == b ]; echo "== $?"
x=1; [ -v x ]; echo "-v x $?"
[ -v nosuchvar ]; echo "-v unset $?"
a=(1 2); [ -v 'a[1]' ]; echo "-v a[1] $?"; [ -v 'a[5]' ]; echo "-v a[5] $?"
[ ! 1 -eq x ] 2>/dev/null; echo "! with a bad integer $?"
[ \( 1 -eq x \) ] 2>/dev/null; echo "grouped bad integer $?"
[ 1 -eq 1 -a x -eq 1 ] 2>/dev/null; echo "-a bad integer $?"
echo "-- [[ ]] numeric operands are arithmetic"
y=4
[[ 1+1 -eq 2 ]]; echo "$?"; [[ x -eq 0 ]]; echo "$?"; [[ "" -eq 0 ]]; echo "$?"
[[ 010 -eq 8 ]]; echo "$?"; [[ y-1 -gt 2 ]]; echo "$?"
[[ 1/0 -eq 1 ]] 2>/dev/null; echo "division $?"
echo "-- numeric arguments"
for v in abc "" " 5" -1 255 256 300 99999999999999999999; do
	( exit "$v" ) 2>/dev/null; printf 'exit [%s] %s; ' "$v" "$?"
	f() { return "$1"; }; f "$v" 2>/dev/null; printf 'return %s\n' "$?"
done
set -- a b; shift abc 2>/dev/null; echo "shift abc $? $#"
set -- a b; shift 5; echo "shift 5 $? $#"
set -- a b; shift -1 2>/dev/null; echo "shift -1 $? $#"
set -- a b; shift 0; echo "shift 0 $? $#"
for i in 1 2; do break 0 2>/dev/null; echo "in $i"; done; echo "break 0 $?"
for i in 1 2; do continue -1 2>/dev/null; echo "in $i"; done; echo "continue -1 $?"
for i in 1; do for j in 1; do break 5; done; echo inner-after; done; echo "break 5 $?"
"$self" -c 'for i in 1 2; do break abc; echo in; done; echo after' 2>/dev/null; echo "break abc $?"
( ulimit -n abc 2>/dev/null; echo "ulimit abc $?"; ulimit -n )
( ulimit -S -n 100; echo "soft $(ulimit -S -n)"; h=$(ulimit -H -n); [ "$h" != 100 ] && echo "hard kept" )
( ulimit -n 200; echo "both $(ulimit -S -n) $(ulimit -H -n)" )
echo "-- read"
printf 'héllo\n' > u8; read -n 2 x < u8; echo "[$x]"; read -N 3 x < u8; echo "[$x]"
printf 'héllo\n' | { read -n 2 x; read -r rest; echo "[$x] [$rest]"; }
printf 'aé\\éb\n' | { read -n 3 x; echo "[$x]"; }
read -u abc x 2>/dev/null; echo "read -u abc $?"
read -u 9 x 2>/dev/null; echo "read -u closed $?"
read -t 0.1 -u 0 x < /dev/null; echo "read -t eof $?"
echo "-- mapfile"
mapfile -t m <<< "$(seq 3)"; echo "${#m[@]} ${m[2]}"
seq 5 | { mapfile -n 2 -t m; read -r next; echo "${m[*]} next=$next"; }
mapfile -u 9 m 2>/dev/null; echo "mapfile -u closed $?"
mapfile -n abc m < /dev/null 2>/dev/null; echo "mapfile -n abc $?"
echo "-- set"
set - a b; echo "$1 $# $?"
set -; echo "set - alone $# $?"
set -e -o nounset; echo "-e -o nounset $?"; set +eu
set -eo nounset; echo "-eo nounset $?"; set +eu
set -a; envvar=exported; set +a; sh -c 'echo "child sees [$envvar]"'
notexp=no; sh -c 'echo "child sees [$notexp]"'
set -B; echo "-B $?"; set -h; echo "-h $?"
cd /; rm -rf "$d"
