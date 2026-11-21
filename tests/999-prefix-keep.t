# An export or a readonly of a name a prefix holds keeps the prefix's
# binding, as bash keeps it: `S=tmp export S` ends exported with tmp, as
# does a function the prefix runs that exports the name, and
# `S=tmp readonly S` ends readonly with tmp and says nothing. On eval or
# source the binding goes with the command, as in bash. hibr put the old
# value back over every one of these, deleted a readonly variable that had
# been unset, and printed an error restoring one that had not (Gitea #602,
# items 5 and 7). Compared against bash.

S2=tmp readonly S2; declare -p S2
K2=keep; K2=tmp readonly K2; declare -p K2
export K3=keep; K3=tmp readonly K3; declare -p K3
r5() { readonly V5; }; V5=keep; V5=tmp r5; declare -p V5
U5=tmp declare -r U5; declare -p U5
T5=tmp typeset -r T5; declare -p T5

S=keep; S=tmp export S; declare -p S
unset S; S=tmp export S; declare -p S
export S=keep; S=tmp export S; declare -p S
g() { export V; }; V=keep; V=tmp g; declare -p V
h() { export V2=other; }; V2=keep; V2=tmp h; declare -p V2
D=keep; D=tmp declare -x D; declare -p D
F=keep; F=tmp export -n F; declare -p F
G=keep; G=tmp export G=new; declare -p G
H=keep; H=tmp command export H; declare -p H
J=keep; J=tmp builtin export J; declare -p J
L=keep; L=tmp typeset -x L; declare -p L
P1=keep; P1=tmp export P1 && echo "next command sees P1=$P1"; declare -p P1
N=keep; N=tmp export N; N=tmp2 true; declare -p N
X1=a X1=b export X1; declare -p X1
o() { M=in export M; }; M=keep; M=out o; declare -p M
k() { local Z; export Z; }; Z=keep; Z=tmp k; declare -p Z
i() { eval 'export V3'; }; V3=keep; V3=tmp i; declare -p V3
dx() { declare -x D2; }; D2=keep; D2=tmp dx; declare -p D2
ar=(1 2); ar=tmp export ar; declare -p ar
ar2=(1 2); ar2=tmp readonly ar2; declare -p ar2

E2=keep; E2=tmp declare -gr E2; declare -p E2
gr() { declare -gr E4; }; E4=keep; E4=tmp gr; declare -p E4

W=keep; W=tmp eval 'export W'; declare -p W
C1=keep; C1=tmp command eval 'export C1'; declare -p C1
B1=keep; B1=tmp builtin eval 'export B1'; declare -p B1
A=keep; A=tmp eval 'readonly A'; declare -p A
o2() { export M2; }; M2=keep; M2=out eval o2; declare -p M2
printf 'export Q\nreadonly Q4\n' > "${TMPDIR:-/tmp}/prefix-keep.$$"
Q=keep; Q4=keep; Q=tmp Q4=tmp . "${TMPDIR:-/tmp}/prefix-keep.$$"; declare -p Q Q4
rm -f "${TMPDIR:-/tmp}/prefix-keep.$$"
