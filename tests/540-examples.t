# Every example must at least run, and its --help must come out of its
# own declarations. A rotted example is worse than no example.

tmp=/tmp/hibr-ex-$$
mkdir -p "$tmp"
printf '[server]\nhost = example.com\nport = 8080\n\n[auth]\ntoken = abc123\n' > "$tmp/app.ini"
printf 'xx\n' > "$tmp/one"; printf 'yyy\n' > "$tmp/two"

for e in examples/*.hibr; do
  case "$e" in *hibrc) continue ;; esac
  ./build/hibr -n "$e" || echo "does not parse: $e"
done
echo "every example parses"

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
