# JSON as the audit found it wanting (Gitea #462): a surrogate pair is one
# character, a lone surrogate or \u0000 is U+FFFD, an empty array or object
# counts 0 and prints as itself, an empty shell array emits [] and an empty
# -A map {}, and text after the document is malformed input.
json parse x '"😀"'; printf '%s' "$x" | od -An -tx1
json parse y '"a\u0000b\ud800c"'; printf '%s' "$y" | od -An -c
json parse e '[]'; echo "empty array ${#e[@]}"
json parse o '{"a":[],"b":{},"c":[1,2]}'; echo "nested ${#o[a][@]} ${#o[b][@]} ${#o[c][@]}"
json get o .a; json get o .b; json get o .c
a=(); json emit a; declare -A h; json emit h
json parse t '{"a":1} junk' 2>/dev/null; echo "trailing text $?"
json parse w '{"a":1}  '; echo "trailing blanks $?"
json parse z '"\u12"' 2>/dev/null; echo "short escape $?"
json parse D '{"a":[]}'; D[a]=hello; echo "scalar over [] ${#D[a][@]} [${D[a][@]}] [${D[a]}]"
