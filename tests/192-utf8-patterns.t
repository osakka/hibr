# A pattern's `?` and bracket take one character, not one byte, in every
# place a pattern is used: case, [[ ]], trimming, substitution and file
# names. Compared against bash, which does the same in a UTF-8 locale --
# so this script sets one for it; hibr reads text as UTF-8 whatever the
# locale says (ADR 0021). Gitea #296.
export LC_ALL=C.UTF-8
shopt -s extglob

for v in "é" "aé" "éa" "€" "日本" "a€b" "x"; do
	r=
	case $v in ?) r+=one, ;; esac
	case $v in ??) r+=two, ;; esac
	case $v in [é€日]*) r+=first, ;; esac
	case $v in *[!a-z]) r+=lastnot, ;; esac
	case $v in [à-ü]*) r+=range, ;; esac
	[[ $v == ?(a)? ]] && r+=ext,
	[[ $v == +([!x]) ]] && r+=plus,
	echo "$v: $r"
done

u="héllo wörld"
echo "<${u//?/.}> <${u#??}> <${u%?}> <${u##*[ö]}> <${u%%[é]*}>"
echo "<${u/[éö]/E}> <${u//[éö]/E}> <${u/#h?/X}> <${u/%?d/Y}>"
echo "<${u//[[:alpha:]]/a}>"

d=$(mktemp -d)
touch "$d/é.txt" "$d/ab.txt" "$d/€.txt"
cd "$d"
echo ?.txt
echo [é€]*
echo ??.txt
cd / && rm -rf "$d"
