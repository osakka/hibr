mod load ./build/mods/sys.so
ml=$(mod list); rsub "$ml" "abi [0-9]+" "abi N"
upper module builtins work
echo "SYS_MOD=$SYS_MOD"
type upper
mod drop sys
upper should now fail
