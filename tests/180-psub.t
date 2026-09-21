cat <(echo from-process-substitution)
diff <(printf "a\nb\n") <(printf "a\nb\n") && echo "identical"
wc -l < <(printf "1\n2\n3\n")
while read line; do echo "read: $line"; done < <(printf "x\ny\n")
