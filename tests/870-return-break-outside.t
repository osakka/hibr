# return outside a function or sourced file, and break or continue outside a
# loop, are reported and the script runs on, as in bash. A function cannot
# break its caller's loop, and neither can a ( ) subshell; $( ), a pipeline
# and & can, since each only ends itself.
return 3; echo "after return: $?"
break; echo "after break: $?"
continue 2; echo "after continue: $?"
eval "return 5"; echo "after eval return: $?"
(return 3); echo "subshell return: $?"
x=$(return 2; echo sub); echo "substitution return: [$x] $?"
f() { break; echo "in f: $?"; }
for i in 1 2; do f; echo "loop $i"; done
g() { return 4; echo never; }; g; echo "function return: $?"
h() { (return 6); echo "subshell in function: $?"; }; h
for i in 1 2 3; do (break; echo "subshell went on"); echo "i=$i"; done
for i in 1; do x=$(break; echo no); echo "substitution break: [$x]"; done
for i in 1 2; do true | break; echo "pipeline i=$i"; done
for i in 1 2; do eval break; echo no; done; echo "eval break: out"
for i in a b; do for j in 1 2; do break 2; done; done; echo "break 2: $i$j"
for i in 1 2 3; do for j in 1 2; do continue 2; echo no; done; echo no; done; echo "continue 2: $i$j"
t=$(mktemp)
printf 'echo s1\nreturn 7\necho s2\n' > "$t"; . "$t"; echo "sourced return: $?"
printf 'break\n' > "$t"
for i in 1 2; do . "$t"; echo "no $i"; done; echo "sourced break: out"
rm -f "$t"
echo "end"
