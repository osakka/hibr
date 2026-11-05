# Every glyph the blit set draws is named by Blit's id, and the table a desktop
# falls back on is generated from Blit's catalogue, never edited (Gitea #121).
# What Blit has deprecated is not in that table, so naming one fails here.
here=${0%/*}
root=$here/..
g=$root/programs/desktop/wm/glyphs.hibr
json parse T < "$root/programs/desktop/wm/blit-glyphs.json"
n=0
bad=0
while read -r w name ids; do
	[ "$w" = dt_bg ] || continue
	for id in $ids; do
		n=$((n + 1))
		[ -n "${T["glyphs"]["$id"]-}" ] || { echo "no such glyph: $id"; bad=1; }
	done
done < "$g"
[ "$n" -gt 200 ] && echo "ids named: enough"
[ "$bad" = 0 ] && echo "every id is in the table"
grep -rl 'U0010[0-9A-Fa-f]' "$root/programs/desktop" --include='*.hibr' || echo "no Blit codepoint is written by hand"
python3 "$root/tools/blitglyphs.py" --check > /dev/null && echo "the table agrees with the newest Blit"
