# declare -F lists functions the way bash does, and names one only if it
# exists; declare -f prints a definition that reads back in as the same
# function. hibr prints it as it was written where bash reformats it, so
# what is compared is what the round trip does, not the text itself.
zf() { echo "zf ran with $1"; }
af() {
	local x=1
	echo "af: $x"
}
declare -F
declare -F af
declare -F nosuch
echo "status $?"
def=$(declare -f zf)
unset -f zf
declare -F zf || echo "gone"
eval "$def"
zf again
