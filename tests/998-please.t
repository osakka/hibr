# please and hibr-run, its root helper (Gitea #406), with no pkexec and no
# root: the helper is run as the person, which is exactly what it does
# under pkexec but for the user, and please against a pkexec that only
# says what it was asked to run.
d=$(mktemp -d)
mkdir "$d/bin" "$d/help" "$d/here"
sed "1s|@HIBR@|$HIBR|" programs/run.hibr > "$d/help/hibr-run"
sed -e "1s|@HIBR@|$HIBR|" -e "s|@HELPDIR@|$d/help|" programs/please.hibr > "$d/please"
sed -e "1s|@HIBR@|$HIBR|" -e "s|@HELPDIR@|$d/help|" -e 's|\$OSTYPE|darwin23|' \
	programs/please.hibr > "$d/please-mac"
chmod 755 "$d/help/hibr-run" "$d/please"
printf '#!/bin/sh\nfor a in "$@"; do printf "[%%s]" "$a"; done; echo\n' > "$d/bin/pkexec"
chmod 755 "$d/bin/pkexec"
r() { "$HIBR" "$d/help/hibr-run" "$@"; }
p() { TERM=dumb LANG=C PATH="$d/bin:$PATH" "$HIBR" "$d/please" "$@"; }

echo "-- the helper: the folder, TERM and LANG it was given, the system's PATH"
r "$d/here" xterm-kitty C.UTF-8 -- sh -c 'echo "${PWD##*/} $TERM $LANG $PATH"'
echo "-- a folder it cannot enter is the home folder instead"
HOME=$d r /no/such/folder dumb C -- sh -c '[ "$PWD" = "$HOME" ] && echo home'
echo "-- the command's status is its own"
r / dumb C -- sh -c 'exit 7'
echo "status $?"
echo "-- refused, and nothing run"
r / 'xterm;rm' C -- echo ran 2>&1; echo "status $?"
r / dumb 'C$(x)' -- echo ran 2>&1; echo "status $?"
r / dumb C echo ran 2>&1; echo "status $?"

echo "-- please: what it asks pkexec to run"
cd "$d/here" && p apt-get install 'two words' | sed "s|$d|D|g"
p -u bob ls -l | sed "s|$d|D|g"
p -s | sed "s|$d|D|g"
echo "-- and what it refuses"
p 2>&1; echo "status $?"
p -s ls 2>&1; echo "status $?"
p -u 'bad name' ls 2>&1; echo "status $?"
p -x ls > /dev/null 2>&1; echo "status $?"
echo "-- with no pkexec it says so"
PATH="$d/bin" "$HIBR" -c "rm -f $d/bin/pkexec; . $d/please ls" 2>&1; echo "status $?"

echo "-- on macOS, which has no polkit, it is sudo underneath (Gitea #407)"
mkdir "$d/mac"
printf '#!/bin/sh\nprintf "sudo"; for a in "$@"; do printf " [%%s]" "$a"; done; echo\n' > "$d/mac/sudo"
chmod 755 "$d/mac/sudo"
m() { PATH="$d/mac:$PATH" "$HIBR" "$d/please-mac" "$@"; }
m apt-get install 'two words'
m -u bob ls -l
m -s
m -u bob -s
PATH="$d/mac" "$HIBR" -c "rm -f $d/mac/sudo; . $d/please-mac ls" 2>&1; echo "status $?"
cd / && rm -rf "${d:?}"
