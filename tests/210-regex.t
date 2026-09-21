if match "2026-09-20 London" "([0-9]{4})-([0-9]{2})-([0-9]{2}) (.*)"; then
  echo "whole=${M[0]}"
  echo "y=${M[1]} m=${M[2]} d=${M[3]} where=${M[4]}"
fi
match "nothing here" "[0-9]+" 2>/dev/null
echo "miss status=$?"
match -a "a1 b22 c333" "[0-9]+" nums
echo "all=${nums[@]} n=${#nums[@]}"
match -i "HELLO" "^hello$" && echo "icase ok"
rsub "hello world" "o" "0" out
echo "first=$out"
rsub -g "hello world" "o" "0" out
echo "global=$out"
rsub -g "omar hamdan" "([a-z]+) ([a-z]+)" "\2, \1"
match "/var/log/syslog.1" "^(.*)/([^/]+)\.([0-9]+)$" P
echo "dir=${P[1]} file=${P[2]} n=${P[3]}"
