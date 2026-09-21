str upper "hello world"
str lower "MiXeD"
str trim "   padded   " t; echo "[$t]"
str len "abcdef"
str slice "abcdefghij" 2 3
str slice "abcdefghij" -3
str index "hello world" "world"
str index "hello" "zzz"
str replace "a-b-c-d" "-" "+"
str split "one:two:three" ":" parts
echo "parts=${parts[@]} n=${#parts[@]}"
str join parts ", "
str pad "x" 5 "."
str pad "x" -5 "."
str repeat "ab" 3
str starts "filename.txt" "file" && echo "starts"
str ends "filename.txt" ".txt" && echo "ends"
str contains "haystack" "st" && echo "contains"
str starts "abc" "z"; echo "no-start status=$?"
nums=(30 4 200 4 17)
arr len nums
arr sort nums -n; echo "sorted=${nums[@]}"
arr sort nums -n -r; echo "desc=${nums[@]}"
arr uniq nums; echo "uniq=${nums[@]}"
arr reverse nums; echo "rev=${nums[@]}"
arr push nums 999; echo "push=${nums[@]}"
arr pop nums last; echo "pop=$last rest=${nums[@]}"
arr contains nums 17 && echo "has 17"
arr contains nums 5 || echo "no 5"
fn double(int n) -> int { ret $((n * 2)); }
arr map nums double; echo "map=${nums[@]}"
fn big(int n) { [ "$n" -gt 50 ]; }
arr filter nums big; echo "filter=${nums[@]}"
