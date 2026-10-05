# salat: prayer times by a method read from a folder. London and Makkah on
# one day by several methods, Hanafi Asr, a person's own adjustments, a
# method of one's own, a broken one left out, and what is refused.
export TZ=UTC
d=${TMPDIR:-/tmp}/hibr-salat.$$
mkdir -p "$d"
export HIBR_SALAT="$d:mods/salat/methods"
mod load ./build/mods/salat.so
hm() {
	local t x
	t=$1
	for x in $t; do
		if [ "$x" = -1 ]; then printf -- '--:-- '; else printf '%(%H:%M)T ' "$x"; fi
	done
	echo
}
salat list | sort
for m in mwl isna egypt umalqura karachi moonsighting tehran turkey; do
	t := salat times -m "$m" 51.5074 -0.1278 2026-10-05
	printf '%-13s' "$m"; hm "$t"
done
t := salat times -m umalqura 21.4225 39.8262 2026-10-05
printf '%-13s' makkah; hm "$t"
t := salat times -a hanafi 51.5074 -0.1278 2026-10-05
printf '%-13s' hanafi; hm "$t"
t := salat times -j 2,0,0,0,0,-3 51.5074 -0.1278 2026-10-05
printf '%-13s' adjusted; hm "$t"
t := salat times -m mwl 69.6492 18.9553 2026-12-21
printf '%-13s' polar-night; hm "$t"
printf '{"name":"mine","title":"Mine","fajr":16,"isha":16,"adjust":[0,0,2,0,0,0]}\n' > "$d/mine.json"
printf '{"name":"broken","fajr":\n' > "$d/broken.json"
"$SH" -c 'mod load ./build/mods/salat.so; salat list' | sort | grep -c .
"$SH" -c 'mod load ./build/mods/salat.so; t := salat times -m mine 51.5074 -0.1278 2026-10-05; for x in $t; do printf "%(%H:%M)T " $x; done; echo'
salat times -m nosuch 51 0 2>&1; echo "status $?"
salat times 95 0 2>&1; echo "status $?"
salat times 51 0 2026-13-45x 2>&1; echo "status $?"
rm -rf "$d"
