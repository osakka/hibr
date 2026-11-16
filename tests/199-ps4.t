# set -x marks each line with PS4 expanded as bash expands it -- escapes,
# parameters, command substitution, arithmetic -- and quotes an argument
# that would not read back as one word. PS4 starts as "+ "; unset or empty
# there is no prefix. Compared against bash; the trace goes to stdout here.
exec 2>&1
f() { :; }
set -x
f a "b c" "" "it's" x=1 "*"
set +x
PS4='>> $((1 + 2)) $(echo cs) '
set -x
f one
set +x
PS4='[${#PS4}] '
set -x
f two
set +x
PS4=
set -x
f three
set +x
unset PS4
set -x
f four
set +x
echo done
