d=/tmp/hibr-redir-$$
mkdir -p $d
echo one > $d/f
echo two >> $d/f
cat $d/f
wc -l < $d/f
{ echo group1; echo group2; } > $d/g
cat $d/g
ls /definitely-not-here 2>/dev/null || echo "stderr suppressed"
echo both > $d/h 2>&1
cat $d/h
exec 3>$d/i
echo via-fd3 >&3
exec 3>&-
cat $d/i
rm -rf $d
