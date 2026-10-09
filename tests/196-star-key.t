# A map key named * or @ is a key, not the all-form. Compared against bash,
# which gets this right: a *quoted* subscript is a literal key, and so is
# one that merely expands to `*` -- only a subscript *written* as a bare `*`
# or `@` means every entry (Gitea #148).
#
# Nothing here prints a multi-key map's [@] or [*], because hibr's maps are
# ordered and bash's iterate its own hash order: `${M[@]}` of four keys is
# `a b c d` here and `d a b c` there, deliberately, so a test that printed
# one would be comparing map order rather than this.
declare -A M
M["*"]=star
M["@"]=at
M["a"]=aye

echo "the key named star:  [${M["*"]}]"
echo "the key named at:    [${M["@"]}]"
echo "its length:          [${#M["*"]}]"
echo "how many keys:       [${#M[@]}]"

# A subscript that expands to * is a key too: it was not written as one.
i="*"
echo "through a variable:  [${M[$i]}] [${M["$i"]}]"
j="@"
echo "and for at:          [${M[$j]}]"

# The all-form still is one, written bare.
declare -A S
S["only"]=one
echo "bare star joins:     [${S[*]}]"
echo "bare at expands:     [${S[@]}]"

# Every other way of naming the key already worked and must keep working.
echo "default when set:    [${M["*"]:-no}]"
echo "trimmed:             [${M["*"]#s}]"
echo "replaced:            [${M["*"]/star/S}]"
[ -n "${M["*"]+x}" ] && echo "star is set" || echo "star is unset"
[[ -v M["*"] ]] && echo "and -v agrees" || echo "and -v does not"

v="${M["*"]}"
echo "assigned out:        [$v] [${#v}]"

read -r M["*"] <<< "fromread"
echo "read into it:        [${M["*"]}]"

unset M["*"]
echo "after unset:         [${M["*"]}]"
printf 'keys left:           '
printf '%s\n' "${!M[@]}" | sort | tr '\n' ' '
echo

M["*"]=again
echo "and set again:       [${M["*"]}]"

# An ordinary array is untouched by any of this: an unquoted numeric
# subscript is still arithmetic, a quoted one still a literal key.
n=2
declare -a L=(zero one two three)
echo "indexed:             [${L[n]}] [${L["2"]}] [${#L[@]}] [${L[*]}]"
echo "sliced:              [${L[@]:1:2}]"
