# The reference has to keep up with the shell, and the links have to resolve.

help | sed -n '2,$p' | sed 's/^  \([^ ]*\).*/\1/' > /tmp/hibr-bi-$$
while read b; do
  grep -qF "\`$b" docs/builtins.md || echo "undocumented builtin: $b"
done < /tmp/hibr-bi-$$
rm -f /tmp/hibr-bi-$$
echo "every builtin is in the reference"

for f in docs/*.md docs/adr/*.md; do
  d=$(dirname "$f")
  match -a "$(cat "$f")" '\]\([A-Za-z0-9_./-]+\.md' links
  for l in "${links[@]}"; do
    t=${l#](}
    [ -e "$d/$t" ] || echo "broken link in $f: $t"
  done
done
echo "every link resolves"

for a in docs/adr/0*.md; do
  n=$(basename "$a" | cut -c1-4)
  grep -qF "($(basename "$a"))" docs/adr/README.md || echo "ADR $n not in the index"
done
echo "every decision is indexed"
