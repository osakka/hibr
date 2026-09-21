v=abcdefabc
echo "${v/abc/X}|${v//abc/X}|${v/def/}|${v//a/-}"
p=/usr/local/bin
echo "${p//\//:}"
echo "${v/xyz/nope}"
n=hello
echo "${n/l/L}|${n//l/L}"
