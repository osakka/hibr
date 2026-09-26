# Every example must at least run, and its --help must come out of its
# own declarations. A rotted example is worse than no example.

tmp=/tmp/hibr-ex-$$
mkdir -p "$tmp"
printf '[server]\nhost = example.com\nport = 8080\n\n[auth]\ntoken = abc123\n' > "$tmp/app.ini"
printf 'xx\n' > "$tmp/one"; printf 'yyy\n' > "$tmp/two"

for e in examples/*.hibr examples/desktop/*.hibr examples/desktop/control-panel/*.hibr \
         examples/desktop/desk-accessories/*.hibr examples/desktop/control-strip/*.hibr; do
  case "$e" in *hibrc) continue ;; esac
  ./build/hibr -n "$e" || echo "does not parse: $e"
done
echo "every example parses"

# A function defined twice in a script silently replaces the first, which is
# how the desktop's drag and drop once took over the function that draws the
# open menu. No example, and no app, defines one name twice.
for e in examples/*.hibr examples/desktop/*.hibr examples/desktop/apps/*.hibr \
         examples/desktop/control-panel/*.hibr \
         examples/desktop/desk-accessories/*.hibr examples/desktop/control-strip/*.hibr; do
  sed -n 's/^\([A-Za-z_][A-Za-z0-9_]*\)() *{.*/\1/p' "$e" | sort | uniq -d |
    while read -r f; do echo "defined twice in $e: $f"; done
done
echo "no function is defined twice"

# local id=$1 b=${ARR[$id]...} looks right and often runs right: a local
# statement's own words are all expanded before local assigns any of
# them, so a later word reading an earlier word's name in the same
# statement reads whatever it meant before the statement ran, not what
# was just assigned. Masked whenever the caller's own local happens to
# share the name -- which "id" alone did in seven places across two files
# before this check found them, every one shipped and passing real use
# for as long as nothing called it from a context with no such "id" lying
# around. Cheap and approximate (whitespace-split, so a quoted value with
# a space in it is not read correctly), not exhaustive -- see CLAUDE.md's
# own trap entry for the pattern in full.
for e in examples/*.hibr examples/desktop/*.hibr examples/desktop/apps/*.hibr \
         examples/desktop/control-panel/*.hibr \
         examples/desktop/desk-accessories/*.hibr examples/desktop/control-strip/*.hibr; do
  awk '
  /^[[:space:]]*local[[:space:]]/ {
    line = $0
    sub(/^[[:space:]]*local[[:space:]]+/, "", line)
    sub(/[[:space:]]*#.*/, "", line)
    n = split(line, words, /[[:space:]]+/)
    delete seen
    for (i = 1; i <= n; i++) {
      w = words[i]
      if (w == "") continue
      eq = index(w, "=")
      if (eq == 0) { seen[w] = 1; continue }
      name = substr(w, 1, eq - 1)
      val = substr(w, eq + 1)
      for (nm in seen) {
        pat = "\\$\\{?" nm "([^A-Za-z0-9_]|$)"
        if (val ~ pat)
          print FILENAME ":" FNR ": local " name "=" val \
            " reads " nm ", assigned earlier in the same local statement"
      }
      seen[name] = 1
    }
  }' "$e"
done
echo "no local reads a name assigned earlier in the same statement"

for e in examples/fetch.hibr examples/ls-report.hibr examples/conf.hibr examples/workers.hibr; do
  ./build/hibr "$e" --help > /dev/null 2>&1 || echo "no --help: $e"
done
echo "every declared program answers --help"

./build/hibr examples/conf.hibr --file "$tmp/app.ini" -s auth
./build/hibr examples/conf.hibr -f "$tmp/app.ini" -s server -k port
./build/hibr examples/workers.hibr -n 2 -j 2 | tail -1
./build/hibr examples/ls-report.hibr --dir "$tmp" --top 1 | head -1 |
  sed "s|$tmp|DIR|"

rm -rf "$tmp"
