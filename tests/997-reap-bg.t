# A background job's child is reaped when it exits, not when the next job is
# started. Until 0.99.91 the only thing that reaped was ex_bg on its way in,
# so a burst of three left three zombies until the next burst, and a desktop
# -- which forks for a sync, a transfer or a vault command and nothing else --
# collected one an hour (Gitea #123). SIGCHLD sets a flag, and the end of
# every command reaps when the flag is set and this script has jobs: nothing
# is asked of the kernel otherwise.
#
# A state of Z is a zombie. The error redirection goes before the read, or a
# missing file is reported before stderr has been sent anywhere.
state() {
	local st=gone
	read -r _ _ st _ 2> /dev/null < "/proc/$1/stat" || st=gone
	printf '%s' "$st"
}

( : ) &
one=$!
sleep 0.4
printf 'one job: %s\n' "$(state $one)"

( : ) &
a=$!
( : ) &
b=$!
( : ) &
c=$!
sleep 0.5
printf 'a burst of three: %s %s %s\n' "$(state $a)" "$(state $b)" "$(state $c)"

# What must not change: wait still has a status to give, for a job already
# reaped this way, and a job still running is still waited for.
( exit 7 ) &
d=$!
sleep 0.4
wait $d
printf 'wait on a reaped job: status %s\n' "$?"
( sleep 0.3; exit 4 ) &
e=$!
wait $e
printf 'wait on a running job: status %s\n' "$?"

# And a script's own CHLD trap still runs: the flag is set beside the trap's
# own, never instead of it.
trap 'printf "the trap ran\n"' CHLD
( : ) &
sleep 0.4
trap - CHLD
( : ) &
f=$!
sleep 0.4
printf 'reaped after the trap was cleared: %s\n' "$(state $f)"
