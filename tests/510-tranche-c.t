{ echo out; echo err >&2; } |& cat
echo "pipe-both rc=$?"
echo only-out |& cat

TZ=UTC printf '%(%Y-%m-%dT%H:%M:%S)T\n' 1000000000
printf '%(%Y)T\n' 0

printf 'a\nb\nc\nd\n' | { mapfile -t m; echo "mapfile: ${m[1]} n=${#m[@]}"; }
printf 'a\nb\nc\nd\n' | { mapfile -t -n 2 m2; echo "limited: ${m2[*]}"; }
printf 'a\nb\nc\nd\n' | { mapfile -t -s 2 m3; echo "skipped: ${m3[*]}"; }
printf 'x:y:z' | { mapfile -t -d: m4; echo "delim: ${m4[*]} n=${#m4[@]}"; }
printf 'p\nq\n' | { readarray -t m5; echo "readarray: ${m5[*]}"; }

sleep 0.2 &
sleep 0.05 &
wait -n
echo "wait -n rc=$?"
wait
echo "waited for the rest"

trap 'echo [dbg]' DEBUG
echo traced
trap - DEBUG
echo untraced

hash ls
echo "hash rc=$?"
hash -r
echo "hash -r rc=$?"

TZ=UTC env | grep -c '^TZ=UTC'
FOO=bar env | grep -c '^FOO=bar'
