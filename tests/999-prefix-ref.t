# A prefix through a nameref reaches what the reference names, and a
# reference that names nothing -- unset, or an element -- is set aside
# while the prefix stands: the prefix is a plain variable of the
# reference's own name, and the reference comes back untouched. hibr gave
# a program both the target and the raw name, pointed an unset reference
# at the prefix's value, and wrote a variable literally named "a[1]"
# (Gitea #602, items 1-3). Compared against bash.

S=keep; declare -n nr=S
nr=tmp env | grep -E '^(nr|S)=' | sort
nr=tmp sh -c 'echo "child S=[$S] nr=[${nr-unset}]"'
declare -p S nr

declare -n q2=S; declare -n q1=q2
q1=tmp env | grep -E '^(q1|q2|S)=' | sort
declare -p S

a=(1 2); declare -n ra=a
ra=tmp env | grep -E '^(ra|a)=' | sort
declare -p a

X1=a X1=b env | grep -E '^X1='
readonly R=x
R=y env | grep -c '^R='

declare -n n0
n0=S true
S=hello; echo "n0 reads [${n0-}]"
n0=Q env | grep -E '^(n0|Q)=' | sort
f() { echo "in f: n0=[$n0] S=[$S]"; }
n0=tmp f
echo "n0 still names nothing: [${n0-}]"

declare -n r2; declare -n r1=r2
r1=tmp env | grep -E '^(r1|r2)=' | sort
g() { declare -p r1; }
r1=tmp g
declare -p r1

e=(1 2 3); declare -n re='e[1]'
re=tmp env | grep -E '^(re|e)=' | sort
h() { echo "in h: re=[$re] e=[${e[*]}]"; declare -p re; }
re=tmp h
declare -p re e
declare -p 'e[1]' 2>/dev/null || echo "no variable named e[1]"
