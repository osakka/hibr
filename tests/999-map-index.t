# A map is a list, and past HIBR_MPHASH entries it keeps an index and a tail
# in its own first entry (Gitea #73). What that must not change: the order
# entries come back in, the count, what a lookup answers, and what a delete
# leaves -- either side of the threshold, and across it. Recorded: nested
# maps are hibr's own, so there is nothing to compare with.

declare -A m
i=0
while [ "$i" -lt 8 ]; do m["k$i"]=$i; i=$((i + 1)); done
echo "small: ${#m[@]} ${m["k0"]} ${m["k7"]} [${m["k9"]}]"
while [ "$i" -lt 40 ]; do m["k$i"]=$i; i=$((i + 1)); done
echo "grown: ${#m[@]} ${m["k0"]} ${m["k39"]} [${m["k99"]}]"
want=$(printf 'k%s ' $(seq 0 39))
echo "insertion order kept: $([ "${!m[*]} " = "$want" ] && echo yes || echo no)"

# A delete takes the entry out of the index as well.
unset m["k20"]
echo "deleted: ${#m[@]} [${m["k20"]}] ${m["k19"]} ${m["k21"]}"
m["k20"]=again
echo "put back: ${#m[@]} ${m["k20"]}"

# The first entry owns the tail and the index, so removing it hands both to
# the next -- and an append after that must still land at the end.
unset m["k0"]
m["zz"]=last
echo "head gone: ${#m[@]} [${m["k0"]}] ${m["k1"]} ${m["zz"]}"
read -ra ks <<< "${!m[*]}"
echo "zz is last: ${ks[$((${#ks[@]} - 1))]}"

# Every key still answers after all that.
bad=0
i=1
while [ "$i" -lt 40 ]; do
	want=$i
	[ "$i" = 20 ] && want=again
	[ "${m["k$i"]}" = "$want" ] || bad=$((bad + 1))
	i=$((i + 1))
done
echo "every key answers: $bad wrong"

# A copy is its own chain, with its own tail and index.
declare -A c
for k in "${!m[@]}"; do c["$k"]=${m["$k"]}; done
c["new"]=1
echo "copied: ${#c[@]} ${c["k1"]} ${c["zz"]} ${c["new"]}"

# Nested maps index on their own.
declare -A r
i=0
while [ "$i" -lt 30 ]; do
	r["r$i"]["a"]=$i
	r["r$i"]["b"]=x$i
	i=$((i + 1))
done
echo "nested: ${#r[@]} ${r["r0"]["a"]} ${r["r29"]["b"]} [${r["r30"]["a"]}]"
unset r["r5"]
echo "nested delete: ${#r[@]} [${r["r5"]["a"]}] ${r["r6"]["a"]}"
