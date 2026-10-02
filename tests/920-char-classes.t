# POSIX character classes in patterns -- [[:alpha:]], [[:space:]] and the
# rest -- in case, ${...} patterns, [[ == ]] and globbing. hibr read the
# bracket up to its first ], so every class was a set of punctuation and
# none ever matched. Compared against bash.
x='ab 1C_-'
for c in alnum alpha blank cntrl digit graph lower print punct space upper xdigit; do
	case "$x" in *[[:$c:]]*) r=yes ;; *) r=no ;; esac
	echo "$c in case: $r"
done
echo "[${x%%[[:space:]]*}]"
echo "[${x##*[[:space:]]}]"
echo "[${x//[[:digit:]]/#}]"
echo "[${x//[![:alnum:]]/.}]"
echo "[${x//[[:upper:][:digit:]]/^}]"
echo "[${x//[a[:digit:]]/*}]"
[[ $x == ab[[:space:]]* ]] && echo "[[ space: yes"
[[ $x == *[[:punct:]] ]] && echo "[[ punct at the end: yes"
case ']' in []]) echo "] first is a member" ;; esac
case 'b' in [!]a]) echo "and after a negation" ;; esac
case 'q' in [[:bogus:]]) echo bogus ;; *) echo "an unknown class holds nothing" ;; esac
d=$(mktemp -d)
touch "$d/a1" "$d/b2" "$d/cc"
for f in "$d"/[[:alpha:]][[:digit:]]; do echo "glob: ${f##*/}"; done
rm -rf "$d"
