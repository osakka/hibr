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
