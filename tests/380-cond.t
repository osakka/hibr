x="hello world"; e=""; n=5
[[ $x == hello* ]] && echo "glob match"
[[ $x == "hello*" ]] || echo "quoted is literal"
[[ -n $x && -z $e ]] && echo "and"
[[ -z $x || $n -gt 3 ]] && echo "or"
[[ ! -e /nonexistent ]] && echo "not"
[[ ( $n -gt 1 && $n -lt 3 ) || $n -eq 5 ]] && echo "grouping"
[[ $x =~ ^(hel+)o\ (w.*)$ ]] && echo "regex fills M: ${M[1]} ${M[2]}"
re='^[0-9]+$'; [[ 42 =~ $re ]] && echo "regex from variable"
[[ "a b" =~ "a b" ]] && echo "quoted regex"
[[ abc < abd ]] && echo "string less"
[[ $x != *xyz* ]] && echo "not match"
[[ -v x ]] && echo "x is set"; [[ -v nope ]] || echo "nope unset"
[[ $e ]] || echo "empty is false"
[[ $x ]] && echo "unquoted with spaces is one word"
[[ -z $e || $(echo side-effect >&2) ]] && echo "right side skipped"
[[ 10 -gt 9 ]] && [[ 10 > 9 ]] || echo "numeric vs string compare differ"
touch -d '2020-01-01' /tmp/hibr-old; touch /tmp/hibr-new
[[ /tmp/hibr-new -nt /tmp/hibr-old ]] && echo "newer than"
[[ /tmp/hibr-old -ot /tmp/hibr-new ]] && echo "older than"
rm -f /tmp/hibr-old /tmp/hibr-new
[[ a == b ]]; echo "false status=$?"
[[ a -eq ]] 2>/dev/null; echo "error status=$?"
if [[ $n -eq 5 ]]; then echo "in if"; fi
while [[ $n -gt 3 ]]; do n=$((n-1)); done; echo "in while n=$n"
[[ -f /etc/passwd && -r /etc/passwd ]] && echo "file tests"
[[
  $n -eq 3 &&
  -n $x
]] && echo "multi-line"
