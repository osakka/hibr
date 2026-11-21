# A colour scheme's roles (lib/colours.hibr): an LCD's panel and its lit
# colour (Gitea #603) default where a scheme leaves them out, and the
# controls' roles (#604) are empty there, which means worked out from the
# rest; a bad value in any of them refuses the scheme whole. Every shipped
# scheme gives its LCD a near-black panel. And a settings file written
# before a role existed takes it from the scheme it names, never over one
# the file holds.
here=${0%/*}
root=$here/..
. "$root/programs/desktop/lib/colours.hibr"
d=$(mktemp -d)
base='"wall":"#000000","dot":"#000000","bar":"#000000","active":"#ffffff","idle":"#777777","face":"#111111","ink":"#eeeeee"'
printf '{%s}\n' "$base" > "$d/old.json"
cp_colourread old "$d/old.json" &&
	echo "old: lcd ${CP_CO["old"]["lcd"]} lcdink ${CP_CO["old"]["lcdink"]} track [${CP_CO["old"]["track"]}] buttonface [${CP_CO["old"]["buttonface"]}]"
printf '{%s,"lcd":"#010203","lcdink":"#a0ff60","trackfill":"#336699","buttondark":"#000000"}\n' "$base" > "$d/new.json"
cp_colourread new "$d/new.json" &&
	echo "new: lcd ${CP_CO["new"]["lcd"]} lcdink ${CP_CO["new"]["lcdink"]} trackfill ${CP_CO["new"]["trackfill"]} buttondark ${CP_CO["new"]["buttondark"]} knob [${CP_CO["new"]["knob"]}]"
for k in lcd lcdink track trackfill knob buttonface buttonlight buttondark; do
	printf '{%s,"%s":"#12345"}\n' "$base" "$k" > "$d/bad.json"
	cp_colourread "bad$k" "$d/bad.json" && echo "a bad $k was taken"
done
echo "a bad value in any role refuses the scheme"
for f in "$root"/programs/desktop/colours/*.json; do
	n=${f##*/}
	n=${n%.json}
	cp_colourread "$n" "$f" || { echo "$n: refused"; continue; }
	c=${CP_CO["$n"]["lcd"]}
	l=$(( (2126 * 16#${c:1:2} + 7152 * 16#${c:3:2} + 722 * 16#${c:5:2}) / 10000 ))
	[ "$l" -lt 16 ] || echo "$n: its LCD panel $c is not near black"
	[ "${CP_CO["$n"]["lcdink"]}" != "${CP_CODEF["lcdink"]}" ] || [ "$n" = midnight ] ||
		echo "$n: its LCD ink is the default, not chosen"
done
echo "every shipped scheme's LCD is near black, its ink its own"
declare -gA DT_CONFJ
DT_LCD=default DT_LCDINK=default DT_TRACK=default
CP_COLOURS=amber
cp_rolefill
echo "no settings file: $DT_LCD $DT_LCDINK $DT_TRACK"
DT_CONFJ["settings"]["CP_COLOURS"]=amber
DT_CONFJ["settings"]["DT_LCD"]="#202020"
DT_LCD="#202020"
cp_rolefill
echo "written before the roles: $DT_LCD $DT_LCDINK [$DT_TRACK]"
# The scheme in force installed from the catalogue before the roles, then
# updated by it in the same session: the next reading of the schemes (a
# rescan) gives the roles the file never held, and still not DT_LCD.
DT_CONFJ["settings"]["CP_COLOURS"]=ember
CP_COLOURS=ember
grep -v '"lcd' "$root/programs/desktop/colours/ember.json" > "$d/ember.json"
cp_colourread ember "$d/ember.json"
cp_rolefill
echo "an old installed scheme: $DT_LCD $DT_LCDINK"
cp_colourread ember "$root/programs/desktop/colours/ember.json"
cp_rolefill
echo "the same scheme updated: $DT_LCD $DT_LCDINK"
rm -rf "$d"
