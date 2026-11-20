# inspect calls says whether a call's exit status is tested where it stands,
# as the shell tests it -- what the app interface's contract reads to tell a
# request whose answer an app needs from one it only tells (ADR 0050).
HIBR_MODPATH=$(cd "${0%/*}/../build/mods" && pwd)
need inspect || exit 1
f=${TMPDIR:-/tmp}/inspect-tested-$$.hibr
cat > "$f" <<'SRC'
f() {
	if a; then b; fi
	c || return
	d && e
	! g
	while h; do i; done
	until j; do k; done
	y := l
	m | n
	o; p
	{ q; r; } || :
	( s; t ) && :
	u &
	v
}
if w; then :; fi
SRC
r := inspect calls "$f"
for k in "${!r["calls"][@]}"; do
	printf '%s %s\n' "${r["calls"][$k]["name"]}" "${r["calls"][$k]["tested"]}"
done
rm -f "$f"
