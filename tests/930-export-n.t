# export -n takes a name out of the environment and keeps its value; with
# name=value it assigns without exporting.
export A=1 B=2
export -n A
export -n B=3 C=4
echo "A=$A B=$B C=$C"
sh -c 'echo "child A=[$A] B=[$B] C=[$C]"'
export A
sh -c 'echo "again A=[$A]"'
