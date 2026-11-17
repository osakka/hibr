# Every module is built on macOS too, and nothing here compiles for macOS
# before a person runs `brew upgrade`. Two Linux-only spellings reached the
# Mac in one month -- st_mtim in mods/img/img.c (0.99.535) and
# SOCK_NONBLOCK | SOCK_CLOEXEC on a socket() in mods/dbus.c -- and each was
# found by the build failing there. So a source file that uses a name only
# Linux has must show it thought about the rest: an __linux__ or __APPLE__
# branch, or an #ifndef/#ifdef of the name itself for a fallback. This names
# each file that uses one with neither (0.99.546).
[ -d ./src ] && [ -d ./mods ] || { echo "every Linux-only name is guarded"; exit 0; }
names='SOCK_NONBLOCK SOCK_CLOEXEC accept4 pipe2 O_PATH memfd_create F_ADD_SEALS inotify_ prctl SO_PEERCRED MSG_CMSG_CLOEXEC IP_RECVERR epoll_ eventfd timerfd signalfd getrandom O_TMPFILE statx renameat2 TIOCGPTPEER MSG_NOSIGNAL'
bad=0
for f in ./src/*.c ./mods/*.c ./mods/*/*.c; do
	grep -qE '__linux__|__APPLE__' "$f" && continue
	# The code alone: a name in a comment -- hold's wire.c explains the
	# POSIX form it uses by naming the three Linux calls it avoids -- is
	# not a use.
	code=$(awk '/\/\*/ { c = 1 } !c { print } /\*\// { c = 0 }' "$f")
	for n in $names; do
		grep -q -- "$n" <<< "$code" || continue
		grep -qE "#if(n)?def $n\b" <<< "$code" && continue
		echo "Linux-only, unguarded: $n in $f"
		bad=1
	done
done
[ "$bad" = 0 ] && echo "every Linux-only name is guarded"
exit 0
