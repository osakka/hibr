# A window's input handler answers its caller through dt_appcall (ADR 0050:
# an app's own process sends that answer to the desktop as its `took`), and
# timing the call must not take the answer's place: a slow call's own log
# line once came back as the answer, since naming the slowest call sets the
# result slot. Answer and status are the callback's, slow or not.

T=/tmp/hibr-appcallret-$$
mkdir -p "$T"
env -i HOME="$T" PATH=/usr/bin:/bin XDG_STATE_HOME="$T/s" TERM=dumb \
	HIBR_MODPATH="$PWD/build/mods" ./build/hibr -c '
DT_HERE=programs/desktop
. programs/desktop/interface.hibr
DT[1]["app"]=x
fn x_key(int id, k) {
	local t=$((${EPOCHREALTIME/./} + 20000))
	while [ "${EPOCHREALTIME/./}" -lt "$t" ]; do :; done
	[ "$k" = q ] && return 1
	ret "taken $k"
}
for ms in 100000 0; do
	DT_SLOWMS=$ms
	DT_HOLDMS=100000
	RET=
	dt_appcall 1 x_key a 2> /dev/null
	echo "slow mark $ms: status $?, answer [$RET]"
	RET=
	dt_appcall 1 x_key q 2> /dev/null
	echo "slow mark $ms: status $?, answer [$RET]"
done
'
rm -rf "$T"
