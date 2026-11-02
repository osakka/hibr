# The core audit's lexer and parser findings (Gitea #434-#438, #472),
# compared against bash. The end of $( ) is found by parsing what is in it,
# so here-documents, comments, case patterns, $'...' and nested quotes all
# decide where it ends; <( and >( are substitutions only where a word
# starts; =~ treats what was quoted as literal text.
self=$(readlink /proc/$$/exe)
x=$(cat <<EOF2
it's here
EOF2
); echo "1 $x"
y=$(cat <<'Z'
a ) b
Z
); echo "2 $y"
echo "3 $(echo hi # it's a comment
)"
echo "4 $(echo $'it\'s')"
echo "5 $(echo "$(echo "a)b")")"
echo "6 $(case a in a) echo y;; esac)"
echo "7 $((echo a); (echo b))"
echo "8 $(( (1+2) * 3 ))"
echo "9 ${u:-$( echo "x)y" )}"
"$self" -c $'cat <<E\nhi\nE'
z=a/bz; echo "10 ${z/"a/b"/c}"
printf 'echo $(\necho p1)\necho p2\n' | "$self"; echo "piped $?"
printf 'echo `\necho p3`\necho p4\n' | "$self"; echo "piped $?"
printf 'echo a\nif then\n' | "$self" 2> /dev/null; echo "syntax $?"
a=1; b=2; echo "11 $(( a<(b) )) $(( a>(b) ))"
for ((i=0; i<(3-1); i++)); do echo -n "$i "; done; echo
echo "a<(echo hi)b" "${u:-<(echo z)}"
[[ axb =~ "a.b" ]] && echo 12y || echo 12n
[[ ab =~ "a"b ]] && echo 13y || echo 13n
v=a.b; [[ axb =~ ^"$v"$ ]] && echo 14y || echo 14n
[[ 'a(' =~ 'a(' ]] && echo 15y || echo 15n
r="a.b"; [[ axb =~ $r ]] && echo 16y || echo 16n
w=abc; [[ ($w =~ ^a) ]] && echo 17y
h=hello; echo ${h^^[l]} ${h^[h]} ${h^[e]}
H=HELLO; echo ${H,,[LO]} ${H,[H]}
shopt -s extglob
p=x; case x in @($p)) echo 18y;; *) echo 18n;; esac
q='a|b'; case b in @($q)) echo 19y;; *) echo 19n;; esac
case "a b" in @(a b)) echo 20y;; *) echo 20n;; esac
foo-bar() { echo 21; }; foo-bar
for k in a b; { echo "22$k"; }
echo a \
