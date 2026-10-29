# hibr-access, the root helper behind Control Panel > Administrative Access
# (Gitea #406), run as the person on paths of a test's own: the rules it
# writes for polkit, the settings it keeps, every refusal before anything is
# written, and the paths it never takes from root. Nothing here reaches
# /etc/polkit-1 or pkexec.
d=$(mktemp -d)
mkdir "$d/acts" "$d/bin"
cat > "$d/acts/t.policy" <<'P'
<policyconfig>
  <action id="org.test.one"><defaults><allow_active>auth_admin</allow_active></defaults></action>
  <action id="org.test.two"><defaults><allow_active>yes</allow_active></defaults></action>
  <action id="org.hibr.access.apply"><defaults><allow_active>auth_admin</allow_active></defaults></action>
</policyconfig>
P
sed "1s|@HIBR@|$HIBR|" programs/access.hibr > "$d/hibr-access"
chmod 755 "$d/hibr-access"
g=$(id -gn)
a() { "$HIBR" "$d/hibr-access" -r "$d/rules" -s "$d/set" -a "$d/acts" "$@"; }

echo "-- apply, as arguments"
a apply "org.test.one self keep" "org.test.two group $g" 2>&1
echo "status $?"
sed "s/\"$g\"/\"GROUP\"/" "$d/rules"
echo "-- the settings, as apply takes them"
a show | sed "s/ $g / GROUP /"
echo "-- read back unchanged, from standard input"
a show | a apply
a show | sed "s/ $g / GROUP /"
[ -e "$d/rules.bak" ] && echo "the rules before it kept"

echo "-- refused, and nothing written"
cp "$d/rules" "$d/before"
for l in "org.hibr.access.apply admin" "org.test.nope admin" "org.test.one never keep" \
	"org.test.one group" "org.test.one group no_such_group_zz" "org.test.one admin more" \
	'org.test.one"); x("y admin' "org.test.one sometimes"; do
	a apply "$l" 2>&1
	echo "status $?"
done
a apply "org.test.one admin" "org.test.one never" 2>&1
echo "status $?"
cmp -s "$d/rules" "$d/before" && echo "rules unchanged"

echo "-- root never takes a path of the caller's"
printf '#!/bin/sh\necho 0\n' > "$d/bin/id"
chmod 755 "$d/bin/id"
PATH="$d/bin:$PATH" "$HIBR" "$d/hibr-access" -r "$d/x" apply "org.test.one admin" 2>&1
echo "status $?"
[ -e "$d/x" ] || echo "nothing written there"

echo "-- reset"
a reset
[ -e "$d/rules" ] || echo "rules gone"
[ -e "$d/set" ] || echo "settings gone"
[ -e "$d/rules.bak" ] && echo "kept as .bak"
a show
echo "show status $?"
a frobnicate 2>&1
echo "status $?"
rm -rf "${d:?}"
