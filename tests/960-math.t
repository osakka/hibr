# The math module: floating point where $(( )) has only integers. Recorded,
# since every answer is the module's own.
mod load ./build/mods/math.so

echo "--- arithmetic, as a spreadsheet shows it"
math 1/3
math 0.1+0.2
math 7/2
math "2^3^2"
math -2^2
math "(1 + 2) * 3 - 4 / 8"
math 10%3
math 2.5e-7
math 1234567.891
math 1e20
math -s 2 10/3
math -s 0 2.5

echo "--- functions and constants"
math "sqrt(2)"
math "round(3.14159, 2)"
math "round(2.5)"
math "floor(-1.5) + ceil(1.2)"
math "abs(-3) + pow(2, 10)"
math "hypot(3, 4)"
math "ln(e)"
math "log(1000)"
math "round(sin(pi / 2), 6)"

echo "--- comparisons, logic and a choice"
math "1 < 2"
math "2 <= 1 || 3 == 3"
math "!0 && 1 != 1"
A1=1.5
math "A1 > 1 ? 100 : 200"

echo "--- shell variables by name, and arrays in sum avg min max count"
price=2.5 qty=4
math "price * qty"
B=(1 2.5 text 4)
math "sum(B)"
math "avg(B)"
math "count(B)"
math "min(B)"
math "max(B, 10)"
math "sum(1, 2, 3) + count(B)"

echo "--- into the result slot"
x := math "price * 3"
echo "x=$x"

echo "--- what it refuses"
math 1/0; echo "status $?"
math 5%0; echo "status $?"
math "nothere + 1"; echo "status $?"
w=word; math "w * 2"; echo "status $?"
math "nofn(1)"; echo "status $?"
math "sqrt(1, 2)"; echo "status $?"
math "2 +"; echo "status $?"
math "(1 + 2"; echo "status $?"
math "1 2"; echo "status $?"
math "avg()"; echo "status $?"
math; echo "status $?"
deep=$(printf '%.0s(' $(seq 300))1$(printf '%.0s)' $(seq 300))
math "$deep"; echo "status $?"
