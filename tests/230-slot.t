fn add(int a, int b) -> int { ret $((a + b)); }
add 2 3
echo "slot=$RET"
sum := add 10 32
echo "bound=$sum"
fn split() { ret one two three; }
parts := split
echo "array=${parts[@]} n=${#parts[@]}"
fn fact(int n) -> int {
  if [ "$n" -le 1 ]; then ret 1; fi
  sub := fact $((n - 1))
  ret $((n * sub))
}
f := fact 6
echo "fact=$f"
fn noval() { ret; }
noval
echo "empty=[$RET]"
fn pair() { ret left right; }
p := pair
echo "keys=${!p[@]} first=${p[0]} second=${p[1]}"

# ret ends the function on the spot, the same as return -- a return after one
# never runs, and the status seen through := is ret's own (always 0), not
# whatever followed it. A miss signals failure with a bare return; := has
# already cleared the slot, so there is nothing to ret "" for.
fn hit(int c) { [ "$c" = 5 ] && { ret "found"; return 0; }; return 1; }
x := hit 5
echo "hit: x=[$x] status=$?"
x := hit 9
echo "miss: x=[$x] status=$?"
fn deadcode() { ret "v"; return 1; }
y := deadcode
echo "dead code after ret: y=[$y] status=$?"
