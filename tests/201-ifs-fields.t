# Splitting on a non-whitespace IFS keeps empty fields, as POSIX and bash do
# (Gitea #317): each such character ends a field on its own, so two in a row
# make an empty one; IFS whitespace still collapses; a single delimiter at the
# very end makes no field; and read's last variable takes the rest of the
# line, losing a single trailing delimiter only when one field is left. hibr
# dropped every empty field, in read and in word splitting alike, so a
# terse nmcli line with an empty first field moved every field one place left.
show() { printf '%s' "$#:"; for a in "$@"; do printf '[%s]' "$a"; done; echo; }
IFS=: read -ra a <<< ":x:y:"; show "${a[@]}"
IFS=: read -ra a <<< "a::b"; show "${a[@]}"
IFS=, read -ra a <<< ",,c"; show "${a[@]}"
IFS=: read -ra a <<< "::"; show "${a[@]}"
IFS=: read -ra a <<< ":"; show "${a[@]}"
IFS=: read -ra a <<< ""; show "${a[@]}"
IFS=": " read -ra a <<< " a :: b "; show "${a[@]}"
IFS=: read -r p q <<< "::z"; show "$p" "$q"
IFS=: read -r p q <<< "a:b:"; show "$p" "$q"
IFS=: read -r p q <<< "a:b::"; show "$p" "$q"
IFS=: read -r p q <<< "a:b:c:"; show "$p" "$q"
IFS=: read -r p q <<< "a: b :"; show "$p" "$q"
IFS=": " read -r p q <<< "a: b : "; show "$p" "$q"
IFS=": " read -r p q <<< "a::"; show "$p" "$q"
IFS=: read -r p <<< ":a:"; show "$p"
IFS=: read -r p q r <<< "a"; show "$p" "$q" "$r"
read -r p q <<< "  one   two  three  "; show "$p" "$q"
read -ra a <<< "  one   two  "; show "${a[@]}"
IFS=$'\t' read -r p q r <<< $'a\t\tb'; show "$p" "$q" "$r"
x=":a::b:"; IFS=:; set -- $x; show "$@"
x="::"; set -- $x; show "$@"
IFS=": "; x=" : "; set -- $x; show "$@"
x="a : b"; set -- $x; show "$@"
IFS=" "; x="  a  b  "; set -- $x; show "$@"
IFS=:; x="a:'b:c':d"; set -- $x "q:r"; show "$@"
unset IFS; x=" a b "; set -- $x; show "$@"
