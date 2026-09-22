h[users][omar][role]=admin
h[users][omar][city]=London
h[users][ghina][role]=student
echo "${h[users][omar][role]} in ${h[users][omar][city]}"
echo "users: ${!h[users][@]}"
echo "fields: ${!h[users][omar][@]}"
echo "values: ${h[users][omar][@]}"
echo "count: ${#h[users][@]}"
for u in "${!h[users][@]}"; do
  echo "  $u -> ${h[users][$u][role]}"
done
cfg=([host]=localhost [port]=8080 [user]=omar)
echo "${cfg[host]}:${cfg[port]} as ${cfg[user]}"
echo "keys: ${!cfg[@]}"
a=(one two three)
a[5]=six
echo "sparse: ${a[@]} keys=${!a[@]} count=${#a[@]}"
i=1
echo "arith subscript: ${a[i+1]}"
deep[a][b][c][d]=bottom
echo "deep: ${deep[a][b][c][d]} levels=${#deep[a][@]}"
unset h
echo "after unset: [${h[users][omar][role]}] count=${#h[@]}"

echo "--- arithmetic reaches a nested subscript"
declare -A g
g[1][row]=6
g[1][col]=10
g[2][row]=9
echo "read $(( g[1][row] + g[1][col] ))"
(( g[1][row] = 99 ))
echo "wrote ${g[1]["row"]}"
(( g[2][row] += 4 ))
echo "plus ${g[2]["row"]}"
k=1
echo "byvar $(( g[k][col] ))"
echo "missing $(( g[9][nope] + 7 ))"
let 'g[1]["col"] = 55'
echo "let-quoted ${g[1]["col"]}"
