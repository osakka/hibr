d=/tmp/hibr-glob-$$
mkdir -p $d/sub
touch $d/a.c $d/b.c $d/c.txt $d/.hidden $d/sub/deep.c
cd $d
echo *.c
echo *
echo sub/*.c
echo */*.c
echo [ab].c
echo no-such-*.zz
echo "quoted *.c"
for f in *.c; do echo "file: $f"; done
cd /
rm -rf $d
