#!/bin/sh
# Build, verify, install and keep an hibr installation current.
#
#   ./deploy.sh                 build, test, install, set up
#   ./deploy.sh update          rebuild and reinstall if the source changed
#   ./deploy.sh check           is an update available? (exit 1 if so)
#   ./deploy.sh status          what is installed, and from where
#   ./deploy.sh rollback        put the previous installation back
#   ./deploy.sh uninstall       remove it
#   ./deploy.sh auto on daily   update automatically (systemd timer or cron)
#   ./deploy.sh auto off
#
# Options: --prefix DIR  --yes  --quiet  --no-test  --chsh  --no-rc  --no-shells

set -eu

SRC=$(cd "$(dirname "$0")" && pwd)
PREFIX=${HIBR_PREFIX:-/usr/local}
QUIET=0
YES=0
RUNTEST=1
DOCHSH=0
DORC=1
DOSHELLS=1
CMD=
ARG=

while [ $# -gt 0 ]; do
	case $1 in
	install | update | check | status | rollback | uninstall | auto)
		[ -n "$CMD" ] || CMD=$1 ;;
	on | off | hourly | daily | weekly)
		ARG="$ARG $1" ;;
	--prefix) PREFIX=$2; shift ;;
	--prefix=*) PREFIX=${1#--prefix=} ;;
	--yes | -y) YES=1 ;;
	--quiet | -q) QUIET=1 ;;
	--no-test) RUNTEST=0 ;;
	--chsh) DOCHSH=1 ;;
	--no-rc) DORC=0 ;;
	--no-shells) DOSHELLS=0 ;;
	-h | --help)
		awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "$0"
		exit 0 ;;
	*) printf 'deploy: unknown argument: %s\n' "$1" >&2; exit 2 ;;
	esac
	shift
done
[ -n "$CMD" ] || CMD=install
ARG=${ARG# }

MODDIR="$PREFIX/lib/hibr"
MANIFEST="$MODDIR/.deployed"
BACKUP="$MODDIR/.backup"
BIN="$PREFIX/bin/hibr"

say()  { [ "$QUIET" = 1 ] || printf '%s\n' "$*"; }
step() { [ "$QUIET" = 1 ] || printf '== %s\n' "$*"; }
die()  { printf 'deploy: %s\n' "$*" >&2; exit 1; }

# Find the nearest existing ancestor of a path.
existing() {
	d=$1
	while [ ! -e "$d" ] && [ "$d" != "/" ] && [ "$d" != "." ]; do
		d=$(dirname "$d")
	done
	printf '%s\n' "$d"
}

NEEDROOT=0
[ -w "$(existing "$PREFIX")" ] || NEEDROOT=1
if [ "$NEEDROOT" = 1 ] && [ "$(id -u)" = 0 ]; then NEEDROOT=0; fi

# Run a command, with sudo when the prefix is not ours to write.
priv() {
	if [ "$NEEDROOT" = 0 ]; then
		"$@"
	elif [ -t 0 ] && [ "$YES" = 0 ]; then
		sudo "$@"
	elif sudo -n true 2>/dev/null; then
		sudo -n "$@"
	else
		die "writing $PREFIX needs root, and sudo would ask for a password.
    Run it yourself, or allow this one command without a password:
      $(id -un) ALL=(root) NOPASSWD: $(command -v install), $(command -v rm), $(command -v cp), $(command -v mkdir), $(command -v tee)"
	fi
}

# A fingerprint of everything that ends up in the binary or the modules.
srcid() {
	cat "$SRC"/Makefile "$SRC"/include/*.h "$SRC"/src/*.c "$SRC"/mods/*.c \
	    "$SRC"/mods/*/*.c "$SRC"/mods/*/*.h 2>/dev/null |
		cksum | cut -d' ' -f1
}

# Read one field out of the installed manifest.
field() {
	[ -f "$MANIFEST" ] || return 1
	sed -n "s/^$1=//p" "$MANIFEST" 2>/dev/null
}

# The version this source tree would build.
srcver() {
	sed -n 's/^#define HIBR_VER "\(.*\)"/\1/p' "$SRC/include/hibr.h"
}

# Pull the source tree forward when it is a git checkout with a remote.
pull() {
	[ -d "$SRC/.git" ] || return 0
	command -v git >/dev/null || return 0
	git -C "$SRC" remote 2>/dev/null | grep -q . || return 0
	step "fetching"
	if git -C "$SRC" pull --ff-only --quiet 2>/dev/null; then
		say "   source tree fast-forwarded"
	else
		say "   no fast-forward available, building what is here"
	fi
}

# Build with the module directory this prefix implies.
build() {
	step "building for $MODDIR"
	( cd "$SRC" && make PREFIX="$PREFIX" MODDIR="$MODDIR" ) >/dev/null ||
		die "build failed; run 'make' in $SRC to see why"
}

# Run the test suite unless it was waived.
verify() {
	[ "$RUNTEST" = 1 ] || { say "   tests skipped"; return 0; }
	step "testing"
	out=$( cd "$SRC" && tests/run.sh 2>&1 ) || {
		printf '%s\n' "$out" >&2
		die "tests failed; nothing was installed"
	}
	say "   $(printf '%s\n' "$out" | grep -E '[0-9]+ passed' | tail -1)"
	out=$( cd "$SRC" && ./build/hibr tests/self.hibr 2>&1 ) || {
		printf '%s\n' "$out" >&2
		die "self.hibr failed; nothing was installed"
	}
	say "   $(printf '%s\n' "$out" | grep -E '[0-9]+ passed' | tail -1)"
	# The terminal suites make their own pseudo terminals, so they run
	# headless -- but only where python3 exists, which is not everywhere.
	command -v python3 >/dev/null 2>&1 || { say "   pty suites skipped, no python3"; return 0; }
	for t in editor console cat most vi; do
		[ -f "$SRC/tests/$t.py" ] || continue
		out=$( cd "$SRC" && python3 "tests/$t.py" 2>&1 ) || {
			printf '%s\n' "$out" >&2
			die "tests/$t.py failed; nothing was installed"
		}
		say "   $t.py: $(printf '%s\n' "$out" | grep -E '[0-9]+ passed' | tail -1)"
	done
}

# Keep the current installation so it can be put back.
backup() {
	[ -f "$BIN" ] || return 0
	priv rm -rf "$BACKUP"
	priv mkdir -p "$BACKUP"
	priv cp -p "$BIN" "$BACKUP/hibr"
	for m in "$MODDIR"/*.so; do
		[ -f "$m" ] && priv cp -p "$m" "$BACKUP/"
	done
	[ -f "$MANIFEST" ] && priv cp -p "$MANIFEST" "$BACKUP/.deployed"
	return 0
}

# The module names this build produces, in one line.
built_mods() {
	for m in "$SRC"/build/mods/*.so; do
		[ -f "$m" ] || continue
		m=${m##*/}
		printf '%s ' "${m%.so}"
	done
	printf '\n'
}

# Take away modules an earlier deploy installed that this build no longer
# makes. A module renamed or dropped otherwise stays behind for ever, still
# loadable and still shadowing whatever its name shadows; only names this
# tool put there are touched, never anything installed by hand.
sweep() {
	was=$(field modules || echo "")
	[ -n "$was" ] || return 0
	now=" $(built_mods)"
	for m in $was; do
		case "$now" in
		*" $m "*) continue ;;
		esac
		[ -f "$MODDIR/$m.so" ] || continue
		priv rm -f "$MODDIR/$m.so"
		say "   removed $m, which this build no longer makes"
	done
	return 0
}

# Copy the build into the prefix and record what was installed.
place() {
	step "installing into $PREFIX"
	sweep
	( cd "$SRC" && priv make install PREFIX="$PREFIX" MODDIR="$MODDIR" ) >/dev/null ||
		die "install failed"
	printf 'version=%s\nsrcid=%s\nsource=%s\nprefix=%s\nmodules=%s\ndate=%s\n' \
		"$(srcver)" "$(srcid)" "$SRC" "$PREFIX" "$(built_mods)" \
		"$(date -u '+%Y-%m-%dT%H:%M:%SZ')" |
		priv tee "$MANIFEST" >/dev/null
}

# Prove the installed copy runs and finds its own modules.
smoke() {
	step "checking the installed copy"
	v=$( cd / && "$BIN" --version ) || die "installed binary will not run"
	say "   $v"
	for m in "$MODDIR"/*.so; do
		[ -f "$m" ] || continue
		n=${m##*/}
		n=${n%.so}
		( cd / && "$BIN" -c "mod load $n" ) >/dev/null 2>&1 ||
			die "installed binary cannot load the $n module from $MODDIR"
		say "   module $n loads"
	done
	( cd / && "$BIN" -c 'echo ok' ) >/dev/null || die "installed shell cannot run a command"
}

# Put back whatever is in the backup directory.
rollback() {
	[ -f "$BACKUP/hibr" ] || die "no previous installation to go back to"
	step "restoring the previous installation"
	priv cp -p "$BACKUP/hibr" "$BIN"
	for m in "$BACKUP"/*.so; do
		[ -f "$m" ] && priv cp -p "$m" "$MODDIR/"
	done
	[ -f "$BACKUP/.deployed" ] && priv cp -p "$BACKUP/.deployed" "$MANIFEST"
	smoke
	say "rolled back"
}

# Write a starter rc file, never over one that already exists.
starter_rc() {
	[ "$DORC" = 1 ] || return 0
	rc=${HIBR_RC:-$HOME/.hibrc}
	[ -e "$rc" ] && { say "   $rc left alone"; return 0; }
	cat > "$rc" <<'RC'
mod load prompt
PROMPT[format]='$dir$git$duration$status$char'
PROMPT[duration][min]=500

alias ll='ls -lh'
export EDITOR=vim
RC
	say "   wrote $rc"
}

# Offer the shell to the system as a login shell.
register_shell() {
	[ "$DOSHELLS" = 1 ] || return 0
	grep -qxF "$BIN" /etc/shells 2>/dev/null && { say "   already in /etc/shells"; return 0; }
	if printf '%s\n' "$BIN" | priv tee -a /etc/shells >/dev/null 2>&1; then
		say "   added $BIN to /etc/shells"
	else
		say "   could not write /etc/shells; add $BIN yourself to use it as a login shell"
	fi
}

# Change the login shell, but only when a person says so.
maybe_chsh() {
	[ "$DOSHELLS" = 1 ] || return 0
	[ "$(basename "${SHELL:-}")" = hibr ] && { say "   already your login shell"; return 0; }
	if [ "$DOCHSH" = 0 ]; then
		[ -t 0 ] || return 0
		[ "$YES" = 1 ] && return 0
		printf 'Make %s your login shell? A broken login shell can lock you out of a terminal. [y/N] ' "$BIN"
		read -r a || return 0
		case $a in y | Y | yes) ;; *) say "   left your login shell alone"; return 0 ;; esac
	fi
	chsh -s "$BIN" && say "   login shell changed; it takes effect on your next login" ||
		say "   chsh failed; your login shell is unchanged"
}

UNIT="$HOME/.config/systemd/user"
CRONTAG="# hibr-auto-update"

# True when a user systemd session is available to hold a timer.
have_systemd() {
	command -v systemctl >/dev/null 2>&1 &&
		systemctl --user show-environment >/dev/null 2>&1
}

# Install the timer that keeps this installation current.
auto_on() {
	when=daily
	for a in $ARG; do
		case $a in hourly | daily | weekly) when=$a ;; esac
	done
	if have_systemd; then
		mkdir -p "$UNIT"
		cat > "$UNIT/hibr-update.service" <<SVC
[Unit]
Description=Rebuild and reinstall hibr when its source changes

[Service]
Type=oneshot
ExecStart=/bin/sh $SRC/deploy.sh update --yes --quiet --prefix $PREFIX
SVC
		cat > "$UNIT/hibr-update.timer" <<TMR
[Unit]
Description=Update hibr $when

[Timer]
OnCalendar=$when
Persistent=true

[Install]
WantedBy=timers.target
TMR
		systemctl --user daemon-reload
		systemctl --user enable --now hibr-update.timer >/dev/null 2>&1 ||
			die "could not enable the timer"
		say "automatic updates on, $when, via systemd"
		say "  systemctl --user list-timers hibr-update.timer"
		say "  journalctl --user -u hibr-update.service"
	elif command -v crontab >/dev/null 2>&1; then
		case $when in
		hourly) spec='0 * * * *' ;;
		weekly) spec='30 4 * * 0' ;;
		*) spec='30 4 * * *' ;;
		esac
		( crontab -l 2>/dev/null | grep -v "$CRONTAG" || true
		  printf '%s /bin/sh %s update --yes --quiet --prefix %s %s\n' \
			"$spec" "$SRC/deploy.sh" "$PREFIX" "$CRONTAG" ) | crontab -
		say "automatic updates on, $when, via cron"
	else
		die "no systemd user session and no crontab; cannot schedule updates"
	fi
	if [ "$NEEDROOT" = 1 ] && ! sudo -n true 2>/dev/null; then
		say ""
		say "Note: $PREFIX needs root and sudo asks for a password here, so the"
		say "timer will not be able to install. Either deploy somewhere you own"
		say "(--prefix \$HOME/.local) or allow the install commands without a"
		say "password in sudoers."
	fi
}

# Take the timer away again.
auto_off() {
	done_any=0
	if have_systemd && [ -f "$UNIT/hibr-update.timer" ]; then
		systemctl --user disable --now hibr-update.timer >/dev/null 2>&1 || true
		rm -f "$UNIT/hibr-update.timer" "$UNIT/hibr-update.service"
		systemctl --user daemon-reload
		done_any=1
	fi
	if command -v crontab >/dev/null 2>&1 && crontab -l 2>/dev/null | grep -q "$CRONTAG"; then
		crontab -l 2>/dev/null | grep -v "$CRONTAG" | crontab -
		done_any=1
	fi
	[ "$done_any" = 1 ] && say "automatic updates off" || say "automatic updates were not on"
}

# Say whether a timer is in place.
auto_state() {
	if have_systemd && systemctl --user is-enabled hibr-update.timer >/dev/null 2>&1; then
		printf 'systemd timer (%s)\n' \
			"$(systemctl --user show -p NextElapseUSecRealtime --value hibr-update.timer 2>/dev/null | head -1)"
	elif command -v crontab >/dev/null 2>&1 && crontab -l 2>/dev/null | grep -q "$CRONTAG"; then
		echo "cron"
	else
		echo "off"
	fi
}

# The whole build, verify and install sequence.
deploy() {
	build
	verify
	backup
	place
	smoke
}

case $CMD in
install)
	step "deploying hibr $(srcver) from $SRC"
	[ "$NEEDROOT" = 1 ] && say "   $PREFIX needs root, so some steps use sudo"
	deploy
	step "setting up"
	starter_rc
	register_shell
	maybe_chsh
	say ""
	say "hibr $(srcver) is at $BIN"
	case ":$PATH:" in
	*":$PREFIX/bin:"*) ;;
	*) say "note: $PREFIX/bin is not on your PATH" ;;
	esac
	say "turn on automatic updates with: $0 auto on daily"
	;;
update)
	pull
	now=$(srcid)
	was=$(field srcid || echo none)
	if [ ! -f "$BIN" ]; then
		say "nothing installed at $BIN; installing now"
		deploy
		exit 0
	fi
	if [ "$now" = "$was" ]; then
		if [ "$(field source)" != "$SRC" ]; then
			step "same build, recording its new source directory"
			place
		fi
		say "hibr $(field version) is already current"
		exit 0
	fi
	step "updating $(field version || echo '?') -> $(srcver)"
	deploy
	say "updated to $(srcver)"
	;;
check)
	pull
	if [ ! -f "$BIN" ]; then
		say "not installed at $BIN"
		exit 1
	fi
	if [ "$(srcid)" = "$(field srcid || echo none)" ]; then
		say "current: hibr $(field version) from $(field source)"
		exit 0
	fi
	say "update available: $(field version || echo '?') -> $(srcver)"
	exit 1
	;;
status)
	if [ -f "$BIN" ]; then
		printf 'installed   %s\n' "$BIN"
		printf 'version     %s\n' "$(field version || echo unknown)"
		printf 'modules     %s\n' "$MODDIR"
		printf 'source      %s\n' "$(field source || echo unknown)"
		printf 'deployed    %s\n' "$(field date || echo unknown)"
		if [ "$(srcid)" = "$(field srcid || echo none)" ]; then
			printf 'state       current\n'
		else
			printf 'state       source has changed, %s would build %s\n' "$SRC" "$(srcver)"
		fi
		[ -f "$BACKUP/hibr" ] && printf 'rollback    available\n' || printf 'rollback    none kept\n'
	else
		printf 'installed   no (%s)\n' "$BIN"
	fi
	printf 'auto update %s\n' "$(auto_state)"
	;;
rollback) rollback ;;
uninstall)
	step "removing hibr from $PREFIX"
	( cd "$SRC" && priv make uninstall PREFIX="$PREFIX" MODDIR="$MODDIR" ) >/dev/null 2>&1 || true
	priv rm -rf "$MODDIR"
	auto_off
	say "removed; ~/.hibrc and /etc/shells were left as they are"
	;;
auto)
	case $ARG in
	*off*) auto_off ;;
	*on* | "") auto_on ;;
	*) die "usage: $0 auto on [hourly|daily|weekly] | auto off" ;;
	esac
	;;
*) die "unknown command: $CMD" ;;
esac
