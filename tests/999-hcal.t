# hcal: calendar systems as data. The Hijri date of a day in each bundled
# calendar, a day's adjustment for local sighting, the way back, and a
# calendar of one's own added from a folder, a broken one left out.
export TZ=UTC
d=${TMPDIR:-/tmp}/hibr-hcal.$$
mkdir -p "$d"
export HIBR_CALENDARS="$d:mods/hcal/calendars"
mod load ./build/mods/hcal.so
hcal list | sort
hcal date 2026-10-05
hcal date -c civil 2026-10-05
hcal date -c astronomical 2026-10-05
hcal date -a 1 2026-10-05
hcal date -a -1 2026-10-05
hcal greg 1448 9 1
hcal greg -a 1 1448 9 1
hcal month 1448 9
hcal name 9
x := hcal date 1791211200
echo "bound: $x"
hcal date 1600-01-01
hcal format '%a %d %b %Y  %id %ib %iY  %iB  %%iY %%' 1791211200
hcal format -c civil '%iY-%im-%id %ie' 1791211200
hcal format -n 'a|b|c|R2|e|f|g|h|i|j|k|l' -s 'x|x|x|r2|x|x|x|x|x|x|x|x' '%iB %ib' 1791211200
y := hcal format '%iY' 1791211200
echo "bound: $y"
hcal format 2>&1; echo "status $?"
hcal date -c nosuch 2026-10-05 2>&1; echo "status $?"
printf '{"name":"fatimid","title":"Fatimid","kind":"tabular","epoch":1948439,"leap":[2,5,8,10,13,16,19,21,24,27,29]}\n' > "$d/fatimid.json"
printf '{"name":"broken","kind":"tabular"\n' > "$d/broken.json"
"$SH" -c 'mod load ./build/mods/hcal.so; hcal list' | sort
"$SH" -c 'mod load ./build/mods/hcal.so; hcal date -c fatimid 2026-10-05'
rm -rf "$d"
