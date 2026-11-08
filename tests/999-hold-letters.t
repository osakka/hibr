# Every message hold's wire carries is one letter, and a letter that names
# two messages is a trap waiting: HD_FRM3 shipped in 0.99.467 as 'K', which
# HD_HUDQ already was. They never met -- one is a request on a connection
# of its own -- which is exactly why nothing noticed (Gitea #486).
declare -A seen
dup=
while IFS= read -r l; do
	[[ $l =~ ^[[:space:]]*(HD_[A-Z0-9]+)[[:space:]]*=[[:space:]]*\'(.)\' ]] || continue
	n=${BASH_REMATCH[1]} c=${BASH_REMATCH[2]}
	if [ -n "${seen["$c"]-}" ]; then
		dup="$dup ${seen["$c"]}/$n=$c"
	else
		seen["$c"]=$n
	fi
done < mods/hold/hd.h
[ ${#seen[@]} -gt 30 ] || echo "too few letters found: ${#seen[@]}"
if [ -n "$dup" ]; then
	echo "letters named twice:$dup"
else
	echo "every hold message has a letter of its own"
fi
