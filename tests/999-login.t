# A login shell reads the system profile and then the first personal one of
# hibr's own two names and ~/.profile, and ~/.hibr_logout on the way out --
# whether or not it is interactive, which is what makes -l usable here at
# all. Recorded: the names and the order are hibr's own (ADR 0039).
#
# HIBR_PROFILE stands in for /etc/profile so this says the same thing on
# every machine; HOME is a folder of its own, since a test that read the
# real one would read whoever is running it.
d=$(mktemp -d)
printf 'echo "system profile"\n' > "$d/etcprofile"
export HIBR_PROFILE="$d/etcprofile"
export HOME="$d"

echo "--- nothing at all without the flag"
"$HIBR" -c 'echo the command'

printf 'echo ".profile"\n' > "$d/.profile"
echo "--- -l with only ~/.profile"
"$HIBR" -l -c 'echo the command'

printf 'echo ".hibr_login"\n' > "$d/.hibr_login"
echo "--- ~/.hibr_login wins over ~/.profile"
"$HIBR" --login -c 'echo the command'

printf 'echo ".hibr_profile"\n' > "$d/.hibr_profile"
echo "--- and ~/.hibr_profile wins over both"
"$HIBR" -l -c 'echo the command'

printf 'echo "logout"\n' > "$d/.hibr_logout"
echo "--- the logout file runs last"
"$HIBR" -l -c 'echo the command'

echo "--- a script, not just -c"
printf 'echo "the script"\n' > "$d/s.hibr"
"$HIBR" -l "$d/s.hibr"

echo "--- what the profile sets reaches the command"
printf 'PROF=set\nexport PROF\n' > "$d/.hibr_profile"
rm -f "$d/.hibr_logout"
"$HIBR" -l -c 'echo "PROF=[$PROF]"'

echo "--- and -n reads none of it, having promised to run nothing"
printf 'echo "should not run"\n' > "$d/.hibr_profile"
"$HIBR" -l -n -c 'echo "parsed only"'
rm -rf "$d"
