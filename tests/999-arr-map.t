# arr map puts each command's result in the element, the way `x := command
# element` binds: a builtin's answer goes into the element and not to the
# screen, a function's only through ret, a program's from what it prints.
# An element whose command left nothing is empty, never the result of the
# call before it, which is what $RET held when map read it (Gitea #422).

a=(p q)
arr map a 'str upper'
echo "builtin: ${a[*]}"

fn wrap(str x) { ret "<$x>"; }
b=(m n)
arr map b wrap
echo "function with ret: ${b[*]}"

quiet() { :; }
str upper stale > /dev/null
c=(1 2)
arr map c quiet
echo "function with nothing: [${c[0]}] [${c[1]}] count ${#c[@]}"

d=(/usr/lib/one /tmp/two)
arr map d basename
echo "program: ${d[*]}"

e=(x y)
arr map e printf '%s %s|' A
echo "arguments after the command: ${e[*]}"

f=(3 4)
arr map f 'nosuchcommand_arrmap' 2> /dev/null
echo "missing command: [${f[0]}] [${f[1]}]"

leftover() { str upper "$1" > /dev/null; }
g=(u v)
arr map g leftover 2> /dev/null
echo "function whose last builtin answered: [${g[0]}] [${g[1]}]"

h=(ab abcd xy wxyz)
fn long(str x) { [ ${#x} -gt 3 ]; }
arr filter h long
echo "filter: ${h[*]}"
