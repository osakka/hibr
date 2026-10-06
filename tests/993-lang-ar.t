# The bundled Arabic catalogue (#68, ADR 0032): it is the one shipped
# translation, so it is checked like code -- every string the desktop draws
# has an Arabic one, none left over from a string that has since changed,
# right to left, and Arabic's own plural categories rather than English's
# two. A string added without translating it fails here. Recorded: the
# counts and the wording are hibr's own.

mod load build/mods/lang.so 2> /dev/null || mod load lang
lang load examples/desktop/lang/ar.json || echo "ar.json does not load"
# The count moves whenever a string is added, and that is 992-strings's own
# business, so it is not recorded here.
i := lang info
rsub -g "$i" "[0-9]+$" "n"
python3 - <<'PY'
import json
# Kept in Latin on purpose: product and protocol names, the project's own
# tagline, an address shown as an example, and a label that is all
# placeholders. Anything else identical to its English is untranslated.
LATIN = {"YouTube", "dBASE", "PIM", "ETag: %s", "PID:    %s", "%s %s…",
         "Highly Intuitive Bash-like Runtime",
         "Nextcloud: https://HOST/remote.php/dav/files/USER"}
d = json.load(open("examples/desktop/lang/ar.json"))
s = d["strings"]
want = [l.rstrip("\n") for l in open("examples/desktop/lang/strings.txt") if l.strip()]
miss = [k for k in want if k not in s]
dead = [k for k in s if k not in want]
flat = [k for k, v in s.items() if not v or (v == k and k not in LATIN)]
stale = [k for k in LATIN if k not in s]
print("untranslated:", ", ".join(miss[:5]) or "none")
print("left over:", ", ".join(dead[:5]) or "none")
print("empty or still English:", ", ".join(flat[:5]) or "none")
print("Latin by name but gone from the catalogue:", ", ".join(stale[:5]) or "none")
PY
# Arabic counts in six categories, not two: the Trash's own message.
for n in 0 1 2 3 11 100; do
	t := lang plural "$n" "The Trash is empty: 1 item deleted" "$n"
	echo "$n: $t"
done
