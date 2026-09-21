inc=/tmp/hibr-src-$$.sh
cat > $inc <<'INC'
echo "sourced sees: $# [$1] [$2]"
shift
echo "after shift: $# [$1]"
INC

set -- keep these
echo "before: [$@]"
. $inc one two
echo "restored: [$@]"
. $inc
echo "inherited: [$@]"
rm -f $inc

( trap 'echo sub-exit' EXIT; echo in-sub )
v=$(trap 'echo cmdsub-exit' EXIT; echo in-cmdsub)
echo "cmdsub got: [$v]"
{ trap 'echo bg-exit' EXIT; echo in-bg; } &
wait
{ trap 'echo pipe-exit' EXIT; echo in-pipe; } | cat

trap 'echo outer-exit' EXIT
( echo plain-sub )
{ echo plain-pipe; } | cat
w=$(echo plain-cmdsub)
echo "no double fire: [$w]"

echo "base16: $((16#ff))"
echo "base2: $((2#1011))"
echo "base8: $((8#777))"
echo "base36: $((36#zz))"
echo "base64: $((64#@)) $((64#_))"
echo "still normal: $((0xff)) $((0755)) $((42))"

( echo "${unsetvar:?must be set}"; echo NOT-REACHED ) 2>/dev/null
echo "guard stopped the subshell"

cd /tmp
echo "tilde pwd: $(echo ~+)"
cd /
echo "tilde old: $(echo ~-)"
echo "tilde user: $(echo ~root)"
echo "tilde unknown stays: $(echo ~nosuchuser___)"
echo "tilde home matches: $([ "$(echo ~)" = "$HOME" ] && echo yes)"
