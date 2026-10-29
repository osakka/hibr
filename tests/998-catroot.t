# hibr-catalogue, the catalogue's root helper (Gitea #407): which verbs and
# options it lets through to the catalogue, run against a catalogue that
# only says what it was asked. No pkexec and no root.
d=$(mktemp -d)
mkdir "$d/bin"
sed -e "1s|@HIBR@|$HIBR|" -e "s|@BINDIR@|$d/bin|" programs/catpriv.hibr > "$d/hibr-catalogue"
printf '#!/bin/sh\nfor a in "$@"; do printf "[%%s]" "$a"; done; echo\n' > "$d/bin/catalogue"
chmod 755 "$d/hibr-catalogue" "$d/bin/catalogue"
c() { "$HIBR" "$d/hibr-catalogue" "$@" 2>&1; echo "status $?"; }

echo "-- the verbs that write the system scope, always at system scope"
c install apps/demo themes/paper
c update
c remove --json apps/demo
c purge apps/demo
c check --repair
echo "-- and each verb's own options"
c remove -y --keep -f apps/demo
c uninstall --yes apps/demo
c purge -n --yes apps/demo
echo "-- refused, and the catalogue never run"
c
c index https://example.com/
c list
c install --system apps/demo
c install --index=x apps/demo
c remove --repair apps/demo
c install -y apps/demo
c purge --force apps/demo
rm -rf "${d:?}"
