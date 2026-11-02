# The address under a cell of a terminal (Gitea #423): one a program marked
# as a link (OSC 8), or one written as text -- a web address, a mailbox --
# carried across rows where a program wrapped it with its own line breaks,
# as Claude Code does its sign-in address. A row is joined to the next only
# when the address reaches the right edge and the next row holds a run of
# address characters alone, so a sentence after an address stays out.
mod load build/mods/pty.so || exit 1
mod load build/mods/term.so || exit 1
u=https://claude.ai/oauth/authorize?code=true\&client_id=9d1c250a-e61b-44d9-88ed-5944d1962f5e\&response_type=code\&redirect_uri=https%3A%2F%2Fconsole.anthropic.com%2Foauth%2Fcode%2Fcallback
w=${u:0:56}; x=${u:56:56}; y=${u:112:56}; z=${u:168}
t := term open -r 12 -c 60 /bin/sh -c '
	printf "%s\n" "see https://example.com/a?b=1, ok" "mail me@example.org now"
	printf "  %s\n" "$1" "$2" "$3" "$4"
	printf "\033]8;;https://osc8.example/x\033\\\\here\033]8;;\033\\\\ text\n"
	printf "%s\n" "ends near the edge https://ex.com/path/to/some/thing/xx" "and then words"
	printf "%s\n" "(see https://en.wikipedia.org/wiki/Shell_(computing))."
	sleep 2' sh "$w" "$x" "$y" "$z"
pump() {
	local g=-1 n=0 q=0
	while [ "$q" -lt 2 ] && [ "$n" -lt 100 ]; do
		term poll "$t" 50
		g2 := term gen "$t"
		if [ "$g2" = "$g" ]; then q=$((q + 1)); else q=0; fi
		g=$g2
		n=$((n + 1))
	done
}
pump
for rc in "0 8" "0 30" "1 10" "2 10" "3 3" "5 4" "6 3" "7 25" "8 2" "9 10"; do
	set -- $rc
	if l := term link "$t" "$1" "$2"; then echo "$1 $2: $l"; else echo "$1 $2: none"; fi
done
[ "$(term link "$t" 3 3)" = "$u" ] && echo "the wrapped address is whole"
term close "$t"
