# What every pass of an idle desktop pays for. Each ticker runs on every
# pass of dt_run, timed, about 45 us each (wm/session.hibr's dt_tickers),
# so a ticker registered at load costs every desktop, idle or not: the
# isolation prototype's three cost 120 to 170 us a pass, a fifth of the
# frame budget, and nothing noticed for two releases (0.99.550, 0.99.551).
# A ticker added here is a cost added to every pass: record it on purpose,
# or register it when the thing it serves first exists.

T=/tmp/hibr-ticks-$$
mkdir -p "$T"
env -i HOME="$T" PATH=/usr/bin:/bin XDG_CONFIG_HOME="$T/c" XDG_DATA_HOME="$T/d" \
	XDG_STATE_HOME="$T/s" TERM=dumb HIBR_MODPATH="$PWD/build/mods" ./build/hibr -c '
d=programs/desktop
. "$d/desktop.hibr"
echo "a bare desktop runs ${#DT_TICKERS[@]} tickers a pass:"
printf "  %s\n" "${DT_TICKERS[@]}"
DT_APPDIRS=("$d/apps"); dt_apps
DT_SYSDIRS=("$d/system"); dt_sysapps
DT_BARDIRS=("$d/menubar"); dt_menubar
CP_PANEDIRS=("$d/control-panel"); cp_panes
CS_MODDIRS=("$d/control-strip"); cs_modules
echo "with this tree loaded, ${#DT_TICKERS[@]}:"
printf "  %s\n" "${DT_TICKERS[@]}"
'
rm -rf "$T"
