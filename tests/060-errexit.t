set -e
echo start
if false; then echo no; fi
false || echo "or-guard fine"
! false
echo "still running"
grep -q nothing /dev/null || true
echo "about to fail"
false
echo "NEVER REACHED"
