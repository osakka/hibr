# hibr-loginset, the login screen's settings helper (Gitea #389), with no
# root and no pkexec: the settings at a scratch path and the shipped
# desktop from this tree, so every check reads what the package ships.
d=$(mktemp -d)
sed -e "1s|@HIBR@|$HIBR|" -e "s|@DESKDIR@|$PWD/programs/desktop|" \
	programs/loginset.hibr > "$d/hibr-loginset"
chmod 755 "$d/hibr-loginset"
f="$d/etc/login.json"
l() { HIBR_SYSDIR= "$HIBR" "$d/hibr-loginset" -f "$f" "$@" 2>&1; echo "status $?"; }

echo "-- nothing set: an empty object"
l show
echo "-- what the screen can read, written"
l set theme meadow colours paper saver matrix glyphs ascii session shell power off
l set title "The Lab" message "Back at nine"
l show
echo "-- an empty value takes a key back to its default"
l set colours "" power ""
l show
echo "-- refused, and the file left as it was"
l set colours nosuch
l set colours ../../etc/passwd
l set theme nosuch
l set saver nosuch
l set glyphs blit
l set session gui
l set power maybe
l set title "$(printf 'bad\033[2Jtitle')"
l set message "$(printf '%061d' 0)"
l set service nosuch-pam-service
l set wallpaper /etc/shadow
l set theme
l tty 0 on
l tty 2 maybe
l frobnicate
l show
echo "-- every key taken out leaves an empty object, which the screen reads"
l set theme "" saver "" glyphs "" session "" title "" message ""
l show
rm -rf "${d:?}"
