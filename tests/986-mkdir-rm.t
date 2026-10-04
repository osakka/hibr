# mkdir and rm are builtins, so a system with no coreutils -- a minimal
# guest with only busybox -- can still make the desktop's folders; they
# must answer as GNU's do, which is what bash runs here. An option not
# done in the builtin goes to the program itself.

d=$(mktemp -d)
cd "$d" || exit 1

mkdir a; echo "mkdir: $?"
mkdir a 2> /dev/null; echo "again: $?"
mkdir -p a; echo "-p existing: $?"
mkdir -p b/c/d; echo "-p deep: $? $([ -d b/c/d ] && echo yes)"
mkdir -pv e/f
mkdir -v g
mkdir -m 700 h; echo "-m: $(stat -c %a h)"
mkdir -m700 h2; echo "-m attached: $(stat -c %a h2)"
mkdir --mode=750 h3; echo "--mode: $(stat -c %a h3)"
mkdir -p -m 711 i/j; echo "-p -m: $(stat -c %a i) $(stat -c %a i/j)"
mkdir -p -m 700 a; echo "-p -m leaves an existing one: $(stat -c %a a)"
mkdir -m u=rwx,go= k; echo "symbolic, from the program: $(stat -c %a k)"
mkdir -- -dash; echo "after --: $([ -d ./-dash ] && echo yes)"
mkdir 2> /dev/null; echo "no operand: $?"
touch file
mkdir -p file 2> /dev/null; echo "-p over a file: $?"
mkdir -p file/x 2> /dev/null; echo "-p under a file: $?"

rm file; echo "rm: $? $([ -e file ] || echo gone)"
rm nothing 2> /dev/null; echo "missing: $?"
rm -f nothing; echo "-f missing: $?"
rm -f; echo "-f alone: $?"
rm 2> /dev/null; echo "no operand: $?"
rm a 2> /dev/null; echo "a directory: $?"
rm -d a; echo "-d empty: $? $([ -e a ] || echo gone)"
rm -d b 2> /dev/null; echo "-d full: $?"
touch b/c/d/f1 b/c/f2
rm -r b; echo "-r: $? $([ -e b ] || echo gone)"
mkdir -p v/w; touch v/w/x v/y
rm -rv v | sort
touch q1 q2; rm -v q1 q2
rm -r . 2> /dev/null; echo "dot: $?"
rm -rf ./.. 2> /dev/null; echo "dotdot: $?"
rm -rf / 2> /dev/null; echo "root: $?"
mkdir -p t/in; touch t/in/keep; ln -s ../t l
rm -r l; echo "a link to a directory: $? $([ -e t/in/keep ] && echo target-kept)"
ln -s t l2
rm -rf l2/ ; echo "-rf through a trailing slash: $? $(ls t | wc -l)"
mkdir -p u; touch u/z
rm -rf u nothing; echo "-rf mixed: $? $([ -e u ] || echo gone)"
touch -- -x; rm -- -x; echo "after --: $? $([ -e ./-x ] || echo gone)"
touch p1; rm -i p1 < /dev/null; echo "-i from the program: $? $([ -e p1 ] && echo kept)"
rm --no-preserve-root -f p1; echo "a long option from the program: $? $([ -e p1 ] || echo gone)"

cd / && command rm -rf "$d"
echo "cleaned: $([ -e "$d" ] || echo yes)"
