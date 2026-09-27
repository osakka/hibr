#!/bin/sh
# Keep osakka/homebrew-hibr's own formula in sync with hibr's latest tag,
# so `brew tap osakka/hibr && brew install hibr` builds whatever was most
# recently released -- without a manual step after every release.sh run.
#
# Two repos, two mirrors, already fully automatic on their own: both
# git.home.arpa/itdlabs/hibr and .../homebrew-hibr push-mirror to their own
# github.com/osakka/* copy on every commit (confirmed: a real Gitea push
# mirror, sync_on_commit true, plus an 8h fallback). The one thing nothing
# already does is regenerate Formula/hibr.rb's own url/version/sha256 when
# a new hibr tag appears -- that is what this script is, run on a schedule
# rather than triggered by the tag itself, since there is no Gitea Actions
# runner on this instance to trigger from (checked directly: Actions is
# enabled on the repo, but zero runners are registered anywhere). Same
# systemd-timer/cron shape deploy.sh's own "auto" already uses to keep an
# install current -- reused, not reinvented.
#
# Reads the *GitHub* mirror's own tags, not the private repo's: that is
# where the formula's url has to point anyway, and it is what makes "wait
# for the private-to-public sync before using a brand new tag" free --
# a tag too new to have synced yet simply is not seen here, and the next
# scheduled run tries again. No special-casing needed for that ordering.
#
#   tools/homebrew-sync.sh            check once, update if behind, quietly
#   tools/homebrew-sync.sh -v         same, but say so either way
#   tools/homebrew-sync.sh --install  install a timer that runs this
#   tools/homebrew-sync.sh --remove   take the timer away again
#   tools/homebrew-sync.sh --status   is the timer on, and when did it last run

set -eu

HIBR_GH=https://github.com/osakka/hibr
FORMULA_REPO=https://git.home.arpa/itdlabs/homebrew-hibr
UNIT="$HOME/.config/systemd/user"
CRONTAG="# hibr-homebrew-sync"
VERBOSE=0

say() { [ "$VERBOSE" = 1 ] && printf '%s\n' "$*" || true; }
die() { printf 'homebrew-sync: %s\n' "$*" >&2; exit 1; }

have_systemd() {
	command -v systemctl >/dev/null 2>&1 && [ -n "${XDG_RUNTIME_DIR:-}" ] &&
		systemctl --user show-environment >/dev/null 2>&1
}

case "${1:-}" in
-v) VERBOSE=1 ;;
--install)
	mkdir -p "$UNIT"
	self=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
	if have_systemd; then
		cat > "$UNIT/hibr-homebrew-sync.service" <<SVC
[Unit]
Description=Sync osakka/homebrew-hibr's formula to hibr's latest tag

[Service]
Type=oneshot
ExecStart=$self
SVC
		cat > "$UNIT/hibr-homebrew-sync.timer" <<TMR
[Unit]
Description=Check for a new hibr release to sync the formula to

[Timer]
OnBootSec=15min
OnUnitActiveSec=6h
Persistent=true

[Install]
WantedBy=timers.target
TMR
		systemctl --user daemon-reload
		systemctl --user enable --now hibr-homebrew-sync.timer >/dev/null 2>&1 ||
			die "could not enable the timer"
		echo "installed: systemd timer, every 6h"
		echo "  systemctl --user list-timers hibr-homebrew-sync.timer"
	elif command -v crontab >/dev/null 2>&1; then
		( crontab -l 2>/dev/null | grep -v "$CRONTAG" || true
		  echo "17 */6 * * * $self $CRONTAG" ) | crontab -
		echo "installed: cron, every 6h"
	else
		die "no systemd user session and no crontab; cannot schedule this"
	fi
	exit 0
	;;
--remove)
	if have_systemd; then
		systemctl --user disable --now hibr-homebrew-sync.timer >/dev/null 2>&1 || true
		rm -f "$UNIT/hibr-homebrew-sync.timer" "$UNIT/hibr-homebrew-sync.service"
		systemctl --user daemon-reload
	fi
	command -v crontab >/dev/null 2>&1 &&
		( crontab -l 2>/dev/null | grep -v "$CRONTAG" || true ) | crontab -
	echo "removed"
	exit 0
	;;
--status)
	if have_systemd && systemctl --user list-timers hibr-homebrew-sync.timer \
	   >/dev/null 2>&1; then
		systemctl --user list-timers hibr-homebrew-sync.timer
	else
		echo "not installed (or no systemd user session)"
	fi
	exit 0
	;;
esac

latest_tag=$(git ls-remote --tags --refs "$HIBR_GH" 2>/dev/null |
	sed -n 's#.*refs/tags/##p' | sort -V | tail -1)
[ -n "$latest_tag" ] || die "could not read tags from $HIBR_GH"
latest_ver=${latest_tag#v}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

git clone --quiet --depth 50 "$FORMULA_REPO" "$tmp/repo" 2>/dev/null ||
	die "could not clone $FORMULA_REPO"
current_ver=$(sed -n 's/^[[:space:]]*version "\(.*\)"/\1/p' "$tmp/repo/Formula/hibr.rb")

if [ "$current_ver" = "$latest_ver" ]; then
	say "up to date: formula already at $current_ver"
	exit 0
fi

say "formula at $current_ver, hibr's latest tag is $latest_tag -- updating"
url="$HIBR_GH/archive/refs/tags/$latest_tag.tar.gz"
tarball="$tmp/hibr-$latest_tag.tar.gz"
curl -fsSL "$url" -o "$tarball" ||
	die "could not fetch $url"
sha=$(sha256sum "$tarball" | cut -d' ' -f1)

sed -i \
	-e "s#^\(  url \"\).*#\1$url\"#" \
	-e "s/^\(  version \"\).*/\1$latest_ver\"/" \
	-e "s/^\(  sha256 \"\).*/\1$sha\"/" \
	"$tmp/repo/Formula/hibr.rb"

git -C "$tmp/repo" add Formula/hibr.rb
git -C "$tmp/repo" -c user.email="osakka@gmail.com" -c user.name="osakka" \
	commit --quiet -m "hibr $latest_ver"
git -C "$tmp/repo" push --quiet origin main
say "pushed: formula now at $latest_ver"
