# What `x := f` binds when f never calls ret (0.99.368): today what f's last
# command left, with a warning naming f once, since from 0.99.370 it binds
# empty, as a program that prints nothing does. `ret -k` is how a function
# passes on what it has -- a list included -- and stays quiet; a function
# that ret's a value, and a miss that returns non-zero, say nothing.
mod load ./build/mods/csv.so
fn f() { str upper hi > /dev/null; }
x := f; echo "f binds [$x]"
x := f; echo "and is named once"
fn g() { str upper hi > /dev/null; ret -k; }
y := g; echo "ret -k passes it on: [$y]"
fn rows() { csv split "a,b,c" > /dev/null; ret -k; }
m := rows; echo "a list too: ${#m[@]} [${m[*]}] ${m[1]}"
fn v() { str upper hi > /dev/null; ret done; }
w := v; echo "ret VALUE: [$w]"
fn miss() { str upper hi > /dev/null; return 1; }
z := miss || echo "a miss is not named"
fn outer() { v; }
o := outer; echo "a caller that does not ret is named, though its callee did: [$o]"
fn typed() -> int { str upper hi > /dev/null; ret -k; }
t := typed; echo "ret -k is held to the declared type: $?"
