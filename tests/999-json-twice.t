# json parse reads standard input to its end every time. On a C library
# whose end-of-file is sticky (macOS), the second `json parse X < file` in
# one process saw nothing, and a desktop restart lost its saved state.
f=${TMPDIR:-/tmp}/hibr-json-twice.$$
printf '{"a":1,"b":[2,3]}\n' > "$f"
json parse A < "$f" && echo "first: ${A[a]}"
json parse B < "$f" && echo "second: ${B[b][1]}"
printf '{"c":"x"}' > "$f"
json parse C < "$f" && echo "third: ${C[c]}"
rm -f "$f"
