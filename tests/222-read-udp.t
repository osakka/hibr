# read on a datagram socket takes a datagram at a time (0.99.404). Each read
# of such a socket empties a whole datagram, so reading a byte at a time kept
# its first byte and lost the rest. -N matches bash, which reads -N in
# blocks; a line and -n are deliberately better than bash, which reads those
# a byte at a time on a socket and keeps only "h".
port=$(( 20000 + $$ % 20000 ))
listen -u -b "$port" L
exec 3<>/dev/udp/127.0.0.1/$port
printf 'hello world\nsecond line\n' >&3
read -t 2 -r -N 8 a <&$L; echo "-N 8: $? [$a]"
printf 'hello world\nsecond line\n' >&3
read -t 2 -r a <&$L; echo "a line: $? [$a]"
printf 'hello world\nsecond line\n' >&3
read -t 2 -r -n 30 a <&$L; echo "-n 30 stops at the newline: $? [$a]"
printf ab >&3; printf cd >&3
read -t 2 -r -N 4 a <&$L; echo "-N 4 across two datagrams: $? [$a]"
read -t 0.3 -r a <&$L; echo "nothing waiting: $? [$a]"
exec 3>&-
