# An exit, a return or an errexit stop inside a condition keeps its own
# status: the if, the loop or the ! around it must not replace it with its own.
(if exit 3; then :; fi); echo "if exit: $?"
(f() { exit 3; }; if f; then :; fi); echo "if f exits: $?"
(while exit 4; do :; done); echo "while exit: $?"
(until exit 5; do :; done); echo "until exit: $?"
(! exit 6); echo "not exit: $?"
f() { if return 3; then :; fi; echo never; }; f; echo "if return: $?"
g() { while return 2; do :; done; echo never; }; g; echo "while return: $?"
k() { ! return 7; }; k; echo "not return: $?"
if false; then :; fi; echo "if false: $?"
while false; do :; done; echo "while false: $?"
! false; echo "not false: $?"
