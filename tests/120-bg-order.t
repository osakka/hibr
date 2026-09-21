echo first
sleep 0.05 &
echo second
wait
echo third
sleep 0.05 & sleep 0.05 &
echo fourth
wait
echo fifth
a=1; b=2; echo "$a$b"
{ echo grouped; } ; echo after-group
