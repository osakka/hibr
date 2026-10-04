# mv is a builtin, like mkdir and rm, so a system with no coreutils can
# still rename -- the desktop's snapshot and settings are written beside
# and renamed into place. It answers as GNU's does, which is what bash
# runs here; a move across filesystems and anything not done here go to
# the program.

d=$(mktemp -d)
cd "$d" || exit 1

touch a; mv a b; echo "rename: $? $([ -e b ] && [ ! -e a ] && echo yes)"
mv b b 2> /dev/null; echo "onto itself: $?"
mkdir d e; touch d/x
mv d e/; echo "into a directory with a slash: $? $(ls e)"
mv e/d e/d/z 2> /dev/null; echo "into itself: $?"
mv -v e/d .
mkdir -p f/d; touch f/d/y
mv d f 2> /dev/null; echo "onto a full directory: $?"
touch g
mv d g 2> /dev/null; echo "a directory over a file: $?"
mv g d; echo "a file into a directory: $? $(ls d)"
mv nothere x 2> /dev/null; echo "missing: $?"
mv 2> /dev/null; echo "no operand: $?"
mv d/g 2> /dev/null; echo "one operand: $?"
touch p q r; mkdir t
mv p q t; echo "many into a directory: $? $(ls t)"
mv r q 2> /dev/null; echo "many, last not a directory... one: $?"
touch s1 s2
mv s1 s2 nodir 2> /dev/null; echo "many into nothing: $?"
mv -v -t t s1 s2
mv -T t/s1 lone; echo "-T: $? $([ -f lone ] && echo yes)"
mkdir u; mv -T lone u 2> /dev/null; echo "-T over a directory: $?"
echo keep > k1; echo new > k2
mv -n k2 k1; echo "-n: $? $(cat k1) $([ -e k2 ] && echo k2-kept)"
mv -f k2 k1; echo "-f: $? $(cat k1)"
ln -s t lt; mv lt lt2; echo "a link moves as itself: $? $([ -L lt2 ] && echo link)"
touch -- -x; mv -- -x y; echo "after --: $? $([ -e y ] && echo yes)"
mkdir w; touch w/in
mv w/ w2; echo "a trailing slash: $? $(ls w2)"
mv --backup=numbered y lone; echo "backups, from the program: $? $(ls lone* | tr '\n' ' ')"

cd / && rm -rf "$d"
echo "cleaned: $([ -e "$d" ] || echo yes)"
