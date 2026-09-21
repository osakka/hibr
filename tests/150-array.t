a=(one two three)
echo "${a[0]}|${a[1]}|${a[2]}|$a"
echo "count=${#a[@]} join=${a[@]} star=${a[*]} len1=${#a[1]}"
a[1]=TWO
echo "${a[@]}"
for x in "${a[@]}"; do echo "q[$x]"; done
b=("with space" second)
for x in "${b[@]}"; do echo "b[$x]"; done
for x in ${b[@]}; do echo "u[$x]"; done
i=2
echo "idx: ${a[i]} ${a[$i]} ${a[i-1]} ${a[0]}"
e=()
echo "empty=${#e[@]}"
echo "miss=[${a[9]}] def=[${a[9]:-none}]"
c=(x)
c[1]=y
c[2]=z
echo "grown: ${c[@]} ${#c[@]}"
