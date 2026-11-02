# An empty match moves past one whole character and stops at the end of the
# subject (Gitea #433): it read one byte past the end, into whatever argument
# followed, and stepped through UTF-8 a byte at a time.
rsub -g abcdefghijklmno '$' X; echo
rsub -g abcdefghijklmn '$' X; echo
rsub -g "é" "x*" "-" | od -An -tx1
match -a "aéb" "x*" r; echo "${#r[@]}"
rsub -g "a.b.c" '\.' /; echo
