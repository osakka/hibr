mod load ./build/mods/sys.so
ml=$(mod list); rsub "$ml" "abi [0-9]+" "abi N"
upper module builtins work
echo "SYS_MOD=$SYS_MOD"
type upper
# mod list has two forms, decided on isatty(1): the one above, which is a
# pipe under this harness and is byte for byte what it has always printed,
# and a fitted one on a terminal. Six of the modules a desktop loads ran
# past eighty columns unfitted and wrapped (Gitea #132). Checked as a
# property rather than recorded line by line, so a module's description
# changing does not re-record this file.
mod load ./build/mods/pty.so
export HIBR_RC=/dev/null
for c in 80 50 36; do
	i := pty spawn -r 24 -c "$c" "$PWD/build/hibr" -c \
		'mod load ./build/mods/console.so > /dev/null
		 mod load ./build/mods/pty.so > /dev/null
		 mod load ./build/mods/img.so > /dev/null
		 mod list'
	o := pty drain $i 2000
	pty close $i
	printf '%s' "$o" | tr -d '\r' |
		awk -v c="$c" 'length($0) > m { m = length($0) }
			END { print c " columns: " (m > 0 && m < c ? "fits" : "TOO WIDE " m) }'
done

mod drop sys
upper should now fail
