# An unquoted subscript that only looks like an operator is a key, as
# hibr's maps have always taken it: a colon or a question mark alone, or a
# tilde inside, is text (the audit's ?: and ~ count only as a conditional
# and a leading complement). bash, whose arrays are indexed, errors.
m=(); k=a:b; m[$k]=1; echo "${!m[*]} ${m[$k]}"
unset m; m=(); k="what?"; m[$k]=2; echo "${!m[*]} ${m[$k]}"
unset m; m=(); k=x~y; m[$k]=3; echo "${!m[*]} ${m[$k]}"
json parse J '{"t":{"12:30":"v","9:05?":"w"}}'
for kk in "${!J[t][@]}"; do echo "$kk ${J[t][$kk]}"; done
a=(5 6 7); i=1; echo "${a[i?2:0]} ${a[~-2]}"
