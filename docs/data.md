# Text, regex and JSON

## Text and arrays without pipelines

```
str upper|lower|trim|len|slice|index|replace|split|join|pad|repeat TEXT …
str starts|ends|contains TEXT PART          # status only, for `if`
arr len|push|pop|sort|uniq|reverse|contains VAR …
arr sort VAR -n -r
arr map VAR FN        # each element replaced by FN's ret value
arr filter VAR FN     # elements kept where FN succeeds
```

All in-process. Each writes its result to `$RET`, to a variable if you name
one, and otherwise to standard output. `printf` (with `-v var`) and `echo -e`
are also built in.

## Regular expressions

POSIX extended regular expressions from libc.

```
match "$line" '([0-9]{4})-([0-9]{2})-([0-9]{2})' D    # groups land in D[1..]
match -i "$s" '^hello'                                # case-insensitive
match -a "$s" '[0-9]+' nums                           # every match
rsub -g "$s" '([a-z]+) ([a-z]+)' '\2, \1' out          # \1–\9 backreferences
```

`match` returns 0 on a match and 1 otherwise; captures go to the named map,
`M` by default — the same place `[[ =~ ]]` uses. Subjects that start with `-`
are fine; only the known flags are taken as options.

## JSON

Documents parse into the map model, so both syntaxes reach them.

```
json parse doc '{"items":[{"name":"gateway","replicas":3}],"ok":true}'
json get doc .items[0].name     # jq-style path
echo ${doc[items][0][name]}     # or subscripts
json keys doc .items;  json len doc .items;  json type doc .ok
json set doc .items[0].replicas 5
json set doc .note "7" -s       # -s forces a string
json emit doc -p                # -p indents
```

Types survive a round trip: numbers, booleans and `null` stay unquoted, strings
are quoted and escaped, and empty `{}` and `[]` keep their shape. A map whose
keys are exactly `0 … n-1` is emitted as an array.

---

[← documentation index](README.md) · [← project README](../README.md)
