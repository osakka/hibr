# json parse into a subscripted name fills that entry with the document, and
# a quoted key stays literal -- what Restart Desktop puts each window back
# with. Recorded: json is hibr's own.
declare -A G
G[3]["board"]="1 2 3"
G[3]["n"]=7
G[3]["sub"]["a"]="x y"
j := json get G .3
echo "$j"
json parse G[9] "$j"
echo "copied: ${G[9]["board"]} / ${G[9]["sub"]["a"]} / n=${G[9]["n"]} / keys ${!G[*]}"
json parse G[3] '{"z":[1,2]}'
echo "replaced: ${!G[3][*]} ${G[3]["z"][1]}"
k=a-b
declare -A H
json parse H["$k"]["in"] '{"q":true}'
echo "quoted key: ${!H[*]} ${H["a-b"]["in"]["q"]}"
json parse H[x] 'nope' 2> /dev/null || echo "malformed text changes nothing: ${!H[*]}"
