self=$(readlink /proc/$$/exe)
d=$(mktemp -d)
echo "-- -c takes its command after every option"
"$self" -c -x 'echo traced' 2>&1
"$self" -ec 'false; echo not reached'; echo "rc=$?"
echo "-- a folder or a binary is not a script"
"$self" "$d" 2>/dev/null; echo "folder rc=$?"
printf 'a\0b\necho no\n' > "$d/bin"; "$self" "$d/bin" 2>/dev/null; echo "binary rc=$?"
echo "-- an interactive shell on a pipe reads no further than its line"
printf 'read x\nhello\necho "x=$x"\n' | "$self" -i 2>/dev/null | grep '^x='
rm -rf "$d"
