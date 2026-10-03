# A background job in a strict function: & sets $!, a special parameter,
# which strict vars must not take for a global the function created.
strict
f() {
	( echo child ) &
	wait "$!"
	echo "waited $?"
}
f
g() { undeclared=1; }
g 2>/dev/null
echo "still refused: $?"
