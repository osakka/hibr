# A desktop reads its savers when one is first wanted, and a rescan (a
# catalogue change, Refresh Desktop) asks for them again: each saver's file
# is read once, a new one is read and listed, a removed one is no longer
# listed and comes back when it does -- and nothing is read twice, which
# strict refused for every function and which emptied the list (Gitea #556).
d=$(mktemp -d)
mkdir -p "$d/c/hibr/savers"
export XDG_CONFIG_HOME=$d/c XDG_DATA_HOME=$d/s
HIBR_SYSDIR=$d/sys
DT_HERE=${BASH_SOURCE%/*}/../programs/desktop
dt_has() { command -v "$1" > /dev/null; }
DT_SAVING=
source "$DT_HERE/wm/saver.hibr" 2>&1
rescan() { [ -z "$DT_SAVING" ] && DT_SAVERREADY=; }
dt_saverload 2>&1
echo "first: ${SV_LIST[*]}"
dt_saverload 2>&1
echo "again: ${SV_LIST[*]}"
rescan
dt_saverload 2>&1
echo "after a rescan: ${SV_LIST[*]}"
printf '%s\n' 'sv_saver mine "Mine" 200' 'MINE_READ=$((MINE_READ + 1))' \
	'fn mine_start(int rows, int cols) { :; }' \
	'fn mine_frame(int rows, int cols) { :; }' > "$d/c/hibr/savers/mine.hibr"
MINE_READ=0
rescan
dt_saverload 2>&1
echo "one installed: ${SV_LIST[*]} (read $MINE_READ)"
rescan
dt_saverload 2>&1
echo "and another rescan: ${SV_LIST[*]} (read $MINE_READ)"
mv "$d/c/hibr/savers/mine.hibr" "$d/mine.hibr"
rescan
dt_saverload 2>&1
echo "removed: ${SV_LIST[*]} (gone ${SV["mine"]["gone"]})"
mv "$d/mine.hibr" "$d/c/hibr/savers/mine.hibr"
rescan
dt_saverload 2>&1
echo "back: ${SV_LIST[*]} (read $MINE_READ)"
rm -rf "$d"
