fn greet(name, greeting = "hello") { echo "$greeting, $name"; }
greet omar
greet omar salaam
fn add(int a, int b) -> int { ret $((a + b)); }
add 2 3
echo "sum=$RET"
add 2 three 2>/dev/null
echo "type error status=$?"
greet 2>/dev/null
echo "missing arg status=$?"
greet a b c 2>/dev/null
echo "too many status=$?"
fn tally(label, ...rest) { echo "$label: ${#rest[@]} items -> ${rest[@]}"; }
tally counts a b c d
tally empty
fn conf(map m, str key) { echo "${m[$key]}"; }
cfg=([host]=srv1 [port]=443)
conf cfg host
conf cfg port
fn sum(...nums) -> int { local t=0; for n in "${nums[@]}"; do t=$((t+n)); done; ret $t; }
total := sum 1 2 3 4 5
echo "total=$total"
fn badret() -> int { ret "nope"; }
badret 2>/dev/null
echo "bad ret status=$?"
old() { echo "untyped still works with $1"; }
old yes
fn at(int? col = "", int row = 1) -> int? { echo "col=[$col] row=$row"; ret "$col"; }
at
at 7 2
at "" 3
at x 2>/dev/null
echo "a word for int? status=$?"
c := at 4
echo "bound=$c"
fn strictint(int n) { :; }
strictint "" 2>/dev/null
echo "empty for plain int status=$?"
fn opt(path? p = "") { echo "p=[$p]"; }
opt
opt /tmp
