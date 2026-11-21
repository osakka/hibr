# The emulator keeps a key cap's bevel, SGR 51:6 raised and 51:7 pressed
# (Blit ADR 0237, Gitea #604), rather than taking a kind past 5 for a thin
# frame: hold renders every held desktop from it, so what it keeps is what
# a Blit display is sent. Asked of the emulator itself with DECRQSS, which
# answers the pen in force.
mod load ./build/mods/pty.so
mod load ./build/mods/term.so
for k in 6 7; do
	t := term open -r 3 -c 60 -- bash -c 'stty raw -echo; printf "\033[51:'"$k"'m\033P\$qm\033\\"; IFS= read -r -d "\\" x; printf "%s" "${x#?}"; printf "\033[0m"; sleep 2'
	r=
	for ((i = 0; i < 100; i++)); do
		term poll "$t" 50
		r := term row "$t" 0
		[[ $r == *m* ]] && break
	done
	echo "51:$k: ${r%%m*}m"
	term close "$t"
done
