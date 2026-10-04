# Runaway recursion is an error, never a crash: a function that calls itself
# for ever, a file that sources itself, an eval that evals itself. bash
# segfaults on the first unless FUNCNEST is set; hibr refuses before the
# stack runs out, ends the script as an arithmetic error would, and inside
# try fails only the command. How deep it got depends on the build, so the
# count is masked.
h=${HIBR:-./build/hibr}
d=$(mktemp -d)
run() { "$h" "$@" > "$d/out" 2>&1; st=$?; sed 's/[0-9][0-9]* calls down/N calls down/' "$d/out"; echo "status $st"; }
run -c 'f() { f; }; f; echo "not reached"'
run -c 'f() { f; }; try f; echo "try kept the script going"'
run -c 'FUNCNEST=20; g() { g; }; g'
run -c 'g() { local FUNCNEST=3; h 1; }; h() { h $(($1 + 1)); }; g'
run -c 'd() { [ $1 -lt 50 ] && d $(($1 + 1)); return 0; }; FUNCNEST=60; d 0 && echo "within FUNCNEST runs"'
printf '. %s\n' "$d/self.hibr" > "$d/self.hibr"
run "$d/self.hibr"
run -c 'e="eval \"\$e\""; eval "$e"'
rm -rf "$d"
