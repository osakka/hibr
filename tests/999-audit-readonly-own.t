# hibr's own builtins that write a variable refuse a readonly one and say
# so, where match, json parse and arr wrote through it or answered 0
# (Gitea #457); and a map parameter given the name of the variable passed
# to it gets the map, where hiding the outer one first left it empty
# (Gitea #461).
readonly mro=keep
match abc b mro 2>/dev/null; echo "match $? [$mro]"
readonly jro=keep
json parse jro '{"a":1}' 2>/dev/null; echo "json $? [$jro]"
readonly -a aro=(1 2)
arr push aro 3 2>/dev/null; echo "arr $? [${aro[*]}]"
fn f(map m) { echo "inside ${#m[@]} [${m[2]}]"; }
m=(1 2 3); f m; echo "outside ${#m[@]}"
fn g(arr a) { echo "inside ${#a[@]}"; }
a=(x y); g a
