# lang: a translation catalogue in a hash, CLDR plural rules, and %s and
# %N$s filled from arguments so a translation can reorder them (#68).
# Recorded: the module is hibr's own.

mod load ./build/mods/lang.so || exit 1
lang get Save
lang load tests/lang/ar-test.json; echo "load: $?"
lang info
lang get Save
lang get "Moved %s to %s" a.txt Docs
for n in 0 1 2 3 10 11 99 100 101 102 111; do printf '%s: ' "$n"; lang plural "$n" "%d files" "$n"; done
lang get "Not in the catalogue %s" x
lang has Save && echo "has Save"
lang has Nope || echo "has not Nope"
x := lang get Save; echo "bound: $x"
lang get "100%% sure"
lang off; lang get Save
printf '{"language": "x", "strings": {"a": ' > /tmp/hibr-lang-bad.$$
lang load /tmp/hibr-lang-bad.$$ 2> /dev/null; echo "broken: $?"
lang get a
rm -f /tmp/hibr-lang-bad.$$
lang 2> /dev/null; echo "usage: $?"
