trap 'echo "exit handler"' EXIT
trap 'echo "got usr1"' USR1
trap
kill -USR1 $$
sleep 0.1
echo body
trap - USR1
echo cleared
