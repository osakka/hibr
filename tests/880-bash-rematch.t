# [[ =~ ]] fills bash's BASH_REMATCH as well as M, and clears it on a miss.
line="2026-09-30 ERROR disk full"
re='^[^ ]+ ([A-Z]+) (.*)$'
[[ $line =~ $re ]] && echo "${BASH_REMATCH[1]}|${BASH_REMATCH[2]}"
echo "count ${#BASH_REMATCH[@]}: ${BASH_REMATCH[0]}"
[[ ab =~ (a)(z)?(b) ]] && echo "unmatched group [${BASH_REMATCH[2]}], count ${#BASH_REMATCH[@]}"
[[ x =~ y ]] || echo "a miss leaves ${#BASH_REMATCH[@]}"
f() { local s=$1; [[ $s =~ ^v([0-9]+)$ ]] && echo "major ${BASH_REMATCH[1]}"; }
f v12
