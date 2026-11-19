# A temporary prefix on a builtin or a function exports the name only while
# the command runs. hibr put the old value back and kept the export the
# prefix had given it, so `S=tmp true` left S exported and every child
# started afterwards inherited it (Gitea #596). And a prefix through a
# nameref saved the reference's own value and put that back into the
# variable it reaches, so `nr=tmp true` left S holding "S". Compared
# against bash.

S=keep; S=tmp true; declare -p S
f() { :; }
T=keep; T=tmp f; declare -p T
U=keep; export U; U=tmp true; declare -p U
X=keep; X=tmp :; /bin/sh -c 'echo "child X=[${X-unset}]"'
A=keep; set -a; A=tmp true; declare -p A; set +a
B=keep; export B; B=tmp declare +x B; declare -p B
C=keep; C=tmp eval 'export C'; declare -p C
D=keep; D=tmp declare -p D; declare -p D
unset N; N=tmp true; declare -p N 2>/dev/null || echo "N unset"
declare -i I=3; I=4 true; declare -p I
IFS=, read -r p q <<< "a,b"; echo "[$p] [$q]"

S2=keep; declare -n nr=S2; nr=tmp true; declare -p S2 nr
unset T2; declare -n nt=T2; nt=tmp true; declare -p T2 2>/dev/null || echo "T2 unset"
export E2=keep; declare -n ne=E2; ne=tmp true; declare -p E2
a=(1 2); declare -n ra=a; ra=tmp true; declare -p a
