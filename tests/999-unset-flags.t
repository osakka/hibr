# unset takes options: -v removes only a variable, -f only a function, -n a
# nameref itself, and -- ends them. With none, a name is the variable, or the
# function when no variable has that name -- never both, as hibr did from the
# start, and it parsed no option at all, so `unset -f f` also tried to remove
# a variable called -f (Gitea #422). A plain name reaches through a nameref.
# A subscripted name still removes one element. Compared against bash.

uf_f() { echo "fn"; }
uf_f=1
unset uf_f; echo "1[${uf_f-unset}]"; uf_f
unset uf_f; uf_f 2>/dev/null || echo "2 gone $?"
uf_g() { echo g; }; uf_g=2
unset -f uf_g; echo "3[${uf_g-unset}]"; uf_g 2>/dev/null || echo "3 gone"
uf_h() { echo h; }; uf_h=3
unset -v uf_h; echo "4[${uf_h-unset}]"; uf_h
unset -v uf_h; uf_h
unset -n uf_h; uf_h
unset -fn uf_h; uf_h 2>/dev/null || echo "5 gone"
x=1; declare -n r=x; unset r; echo "6[${x-unset}] [${r-unset}]"
x=2; unset -n r; echo "7[${x-unset}]"
y=1; unset -n y; echo "8[${y-unset}]"
declare -n r2=nosuch; unset r2; echo "9 $?"
a=(1 2 3); unset 'a[1]'; echo "10 ${a[*]} ${#a[@]}"
declare -A m; m["k"]=v; m["j"]=w; unset m["k"]; echo "11 ${!m[*]}"
unset -v 'a[0]'; echo "12 ${a[*]}"
readonly q=1; unset q; echo "13 $? [$q]"
unset -v 'a b'; echo "14 $?"
unset 'a b'; echo "15 $?"
unset -f 'a b'; echo "16 $?"
unset ''; echo "17 $?"
unset -fv zz; echo "18 $?"
unset -x zz; echo "19 $?"
unset -- -v; echo "20 $?"
uf_k() { echo k; }; unset -f -- uf_k; uf_k 2>/dev/null || echo "21 gone"
p=1; s2=2; unset p s2; echo "22[${p-u}][${s2-u}]"
unset; echo "23 $?"
unset -f; echo "24 $?"
declare uf_d; uf_d() { echo dfn; }; unset uf_d; uf_d
uf_e() { local v=1; unset v; echo "26[${v-u}]"; }; uf_e
t1=1 t2=2; unset -v t1 nosuch t2; echo "27[${t1-u}][${t2-u}] $?"
readonly ro2=1; t3=3; unset ro2 t3; echo "28 $? [${t3-u}]"
unset -vf zz; echo "29 $?"
declare -A h; h["a-b"]=1; h[c]=2; k=a-b
unset h["$k"]; echo "30 ${!h[*]}"
unset -v h; echo "31 [${h[c]-gone}]"
b=(x y z); unset -v 'b[-1]'; echo "32 ${b[*]}"
uf_m() { echo m; }; uf_n() { echo n; }
unset -f uf_m uf_n uf_none; uf_m 2>/dev/null || echo "34 gone"; uf_n 2>/dev/null || echo "34 gone"
unset -fn uf_none; echo "35 $?"
c1=1; unset -vn c1; echo "36[${c1-u}]"
declare -n r3=c2; c2=5; unset -vn r3; echo "37[${c2-u}][${r3-u}]"
