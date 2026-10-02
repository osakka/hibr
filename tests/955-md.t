mod load ./build/mods/md.so
md html -t '# Hello *world*'
printf '%s\n' '- [x] done' '- [ ] todo' '' '| a | b |' '|---|--:|' '| 1 | 2 |' | md html
md html -c -t '~~not struck~~ www.example.com'
md html -t '~~struck~~ www.example.com'
f=$(mktemp)
printf '%s\n' 'Setext' '===' '' '> quote' > "$f"
md html "$f"
md lines "$f"
s := md lines -t $'- one\n  - **two**'
echo "${#s[@]}: ${s[0]} ${s[1]}"
h := md html -t '`code`'
echo "bound: $h"
rm -f "$f"
md lines -t ''
echo "empty: $?"
md html /nonexistent/x.md 2> /dev/null
echo "missing: $?"
md frob 2>&1
echo "status: $?"
