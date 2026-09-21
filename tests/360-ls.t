mod load ./build/mods/ls.so
d=/tmp/hibr-ls-test
rm -rf $d; mkdir -p $d/sub; printf 'x' > $d/small; printf '%02000d' 0 > $d/big
touch $d/.hidden; ln -s small $d/link; chmod 755 $d/big
cd $d
echo "--- names:"; ls
echo "--- all:"; ls -a
echo "--- almost:"; ls -A
echo "--- classify:"; ls -F
echo "--- by size:"; ls -S
echo "--- reversed:"; ls -r
echo "--- long, no user columns:"; ls -l | sed 's/ [^ ]* [^ ]* *[0-9]* [A-Z][a-z][a-z] [ 0-9]* [0-9:]* / /'
echo "--- human size of big:"; ls -lh big | sed 's/.* \([0-9.]*K\) .*/\1/'
echo "--- dir itself:"; ls -d sub
echo "--- slot:"; files := ls -q; echo "${#files[@]}: ${files[*]}"
echo "--- missing:"; ls nope 2>/dev/null; echo "rc=$?"
echo "--- unknown flag:"; ls -Z 2>/dev/null; echo "rc=$?"
echo "--- two dirs:"; ls sub .
cd /; rm -rf $d
