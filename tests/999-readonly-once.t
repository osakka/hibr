# A refused assignment to a readonly variable is reported once. A prefix
# assignment, `r=2 cmd`, said it twice: once on the way in and again when
# the old value was put back, though nothing had been changed. Each form is
# run in a shell of its own, since a refused assignment ends a script, and
# the count of lines it printed is compared rather than the wording.
# Compared against bash.

for c in 'r=2 true' 'r=2 echo hi' 'f() { :; }; r=2 f' 'r=2 /bin/true' \
	'r=2' 'r+=x' 'declare r=3' 'r=(2)' 'r[0]=2'; do
	out=$("$SH" -c "readonly r=1; $c; echo \"st \$? r \$r\"" 2>/dev/null)
	n=$("$SH" -c "readonly r=1; $c" 2>&1 >/dev/null | wc -l)
	echo "$c: [$out] lines $n"
done
"$SH" -c 'readonly r=1; r=2 true; echo "$r"'
