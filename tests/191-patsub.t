# Pattern substitution and trimming over a grid of values and patterns,
# compared against bash, and on a long value, where each replacement used
# to copy the rest of the string once per candidate end: 8 KB took 2.8 s
# and 16 KB 20 s (Gitea #294).
#
# Left out, each a difference that is not this test's: `?` and `[!a]`
# match one *byte* where bash matches a character, so no multibyte value
# meets them, and a lone unquoted backslash matches one here and nothing
# in bash (both Gitea #296); and over an empty value, or as `/%` of
# `!(a)`, bash answers from its own count of how long an extended group
# is -- `@(a|)` matches the empty string and bash still leaves an empty
# value alone -- where this shell asks whether the pattern matches.
shopt -s extglob

vals=("" "a" "abc" "aaa" "banana" "a.b.c" 'x\y' "123abc456" "a*b?c"
      "  sp  ce  " "abab" "zzz")
pats=("" "a" "?" "*" "a*" "*a" "a*b" "[ab]" "[!a]" "[[:digit:]]" "an" "na"
      '\*' '\?' "*(a)" "+(a)" "@(a|b)" "?(x)" "a?" "??" "[0-9]*([0-9])" ' '
      'b*' "*c" 'x\\y' "[a-c]" "z" "[]a]" "[a-]")
for v in "${vals[@]}"; do
	for p in "${pats[@]}"; do
		[ -z "$v" ] && [ "$p" = "?(x)" ] && continue
		printf '%s|%s|<%s><%s><%s><%s><%s><%s><%s><%s><%s><%s>\n' "$v" "$p" \
			"${v/$p/R}" "${v//$p/R}" "${v/#$p/R}" "${v/%$p/R}" \
			"${v#$p}" "${v##$p}" "${v%$p}" "${v%%$p}" \
			"${v//"$p"/R}" "${v//$p}"
	done
done

# Multibyte text, with patterns that do not ask for a single character: an
# empty match carries the whole character after it, never half of one.
u="héllo wörld"
for p in "*(a)" "ö" "l*" "*o" "@(é|ö)" "+(l)" "wör" "*(ö)"; do
	echo "$p <${u/$p/R}><${u//$p/R}><${u#$p}><${u%%$p}>"
done

# An empty pattern replaces nothing, and an empty value is replaced only
# by a pattern that matches it.
e=
s=abc
echo "<${s//""/R}><${s/""/R}><${s/#""/R}><${s/%""/R}>"
echo "<${e//*/R}><${e/*(a)/R}><${e//a/R}>"

# Long values: the answer is summed rather than printed.
big=$(for ((i = 1; i <= 4000; i++)); do printf 'line %d of text\n' "$i"; done)
sum() { printf '%s' "$1" | cksum; }
echo "${#big}"
sum "${big//e/E}"
sum "${big//[0-9]/N}"
sum "${big//l*e/Q}"
sum "${big//$'\n'/ }"
sum "${big/text/TEXT}"
sum "${big#zz}"
sum "${big%zz}"
sum "${big##*of}"
sum "${big%%of*}"
sum "${big/#line/L}"
sum "${big/%text/T}"
small=${big:0:400}
sum "${small//+([0-9])/N}"
sum "${small//*(e)/E}"
