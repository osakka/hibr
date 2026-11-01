# Deployment

`make install` is enough if you only want the binary somewhere. `deploy.sh`
does the whole job: it builds with the right module directory, runs the core
test suites, keeps the previous installation so you can go back, verifies that the
copy it just installed actually works, and can keep itself current.

```text
./deploy.sh                    build, test, install, set up
./deploy.sh update             rebuild and reinstall if the source changed
./deploy.sh check              is an update available?  (exit 1 if so)
./deploy.sh status             what is installed, and from where
./deploy.sh rollback           put the previous installation back
./deploy.sh uninstall          remove it
./deploy.sh auto on daily      keep it current by itself
./deploy.sh auto off
```

Options: `--prefix DIR` (default `/usr/local`), `--yes`, `--quiet`,
`--no-test`, `--chsh`, `--no-rc`, `--no-shells`.

## What an install does

1. Builds with `HIBR_MODDIR` set to the prefix you asked for. This matters:
   the module directory is compiled into the binary, so installing to a
   different prefix without rebuilding produces a shell that cannot find its
   own modules.
2. Runs the core suites and stops if anything fails: `tests/run.sh`, then
   `tests/self.hibr`, then the pseudo-terminal suites — `tests/editor.py`,
   `tests/console.py`, `tests/cat.py`, `tests/most.py`, `tests/hvi.py`,
   `tests/mtr.py` and `tests/mon.py` — which make their own terminals and so
   run headless, and are skipped with a note where there is no `python3`.
   The rest -- the desktop's suites and the sanitizers -- are the release
   gate's, described in [testing.md](testing.md), not an install's.
   Nothing is installed from a tree that does not pass. This is checked rather
   than assumed: a deliberately failing test leaves the installed binary
   byte-identical and exits 1, which is what makes the timer report a failure
   instead of quietly doing nothing.
3. Copies the current installation aside, so `rollback` has somewhere to go.
4. Installs with `make install`, and records a manifest next to the modules:
   version, a checksum of every source file, where it was built from, and
   when. A module an earlier deploy installed that this build no longer
   makes -- renamed or dropped -- is taken away, so it cannot go on
   shadowing anything; one installed by hand is never touched. What lands
   where, under the prefix:

   | path | what |
   |---|---|
   | `bin/hibr` | the shell |
   | `bin/desktop` | a launcher for the desktop |
   | `bin/catalogue`, `bin/vw` | the catalogue and the vault client, each a hibr script under `share/hibr/` |
   | `lib/hibr/*.so` | the modules, and the manifest |
   | `share/hibr/desktop/` | the window manager, its apps and accessories |
   | `share/hibr/calendars/`, `share/hibr/salat/` | the calendar systems and prayer-time methods the `hcal` and `salat` modules read |
   | `share/hibr/keyring.gpg` | the key the catalogue checks its index against |
   | `var/lib/hibr/` | the system scope, where `catalogue --system` installs for every user; `/var/lib/hibr` when the prefix is `/usr`, and left for the catalogue to make with `MKSYSDIR=no` |
   | `bin/please` | runs a command as root ([please.md](please.md)); on macOS always, on Linux only with the prefix `/usr`, beside the helpers below |
   | `lib/hibr/hibr-access`, `hibr-settime`, `hibr-run`, `hibr-catalogue`, `hibr-loginset` | the root helpers `pkexec` runs, Linux with the prefix `/usr` only |
   | `/usr/share/polkit-1/actions/org.hibr.policy` | the polkit actions naming them, the one path outside the prefix, because polkit reads no other |
   | `share/man/man1/hibr.1`, `desktop.1` | `man hibr` and `man desktop`, with the version and module directory filled in |
   | `include/hibr/hibr.h`, `fetch.h` | the module ABI, and the fetch module's interface, for building a module of your own |
5. Runs the installed copy — not the one in the build tree — to check that it
   starts and that **every** module in the installed directory loads, one at a
   time, so an ABI bump that leaves a stale module behind is caught here rather
   than by whoever next uses it.
6. Writes a starter `~/.hibrc` if you do not already have one, never over an
   existing file; offers the binary to `/etc/shells`; and asks, once, whether
   you want it as your login shell.

## Updating

`update` compares a checksum of the source tree against the one recorded at
install time and does nothing if they match. That checksum covers every module
directory, not just `mods/*.c` — a module living in its own directory, as the
prompt, console, cat and trace modules do, would otherwise change without the
update noticing — and every file under `programs/desktop`, since the desktop
is installed too. If the tree is a git checkout with
a remote, it fast-forwards first. Then it repeats the sequence above, tests
included, so an update can never install a tree that fails its own tests.

`auto on` installs a systemd user timer, or a cron entry if there is no user
systemd session, running `update` on the schedule you name — `hourly`, `daily`
or `weekly`.

```text
systemctl --user list-timers hibr-update.timer
journalctl --user -u hibr-update.service
```

One caveat worth knowing: a timer runs as you, so if the prefix needs root the
update needs `sudo` without a password. `deploy.sh auto on` says so at the time
if that is not the case. Deploying somewhere you own avoids the question
entirely:

```text
./deploy.sh --prefix "$HOME/.local"
```

## Going back

`rollback` restores the binary, the modules and the manifest from the copy made
during the last install, then runs the same checks against the restored copy.
The desktop, the man page and the header are not part of that copy: they are
whatever the last install put there.
It is one version deep, which is the version you care about when an update has
just gone wrong.

## Packages

For a machine that should simply have hibr, a package manager is the shorter
way, and keeps it current with everything else:

| | |
|---|---|
| Debian 12, Ubuntu 22.04 or newer, amd64 | the signed apt repository at [osakka.github.io/hibr-apt](https://osakka.github.io/hibr-apt), whose page has the four commands |
| Fedora 36, RHEL 10, openSUSE Leap 15.6 or newer, x86_64 | the signed dnf repository at [osakka.github.io/hibr-rpm](https://osakka.github.io/hibr-rpm) |
| Arch Linux, x86_64 | the signed pacman repository at [osakka.github.io/hibr-arch](https://osakka.github.io/hibr-arch) |
| macOS, or Linux with Homebrew | `brew tap osakka/hibr && brew install hibr` |

The [project README](../README.md#install) has the commands for each. One
key signs the apt, dnf and pacman repositories.

A package installs under `/usr`: the shell in `/usr/bin`, its modules in
`/usr/lib/hibr`, the manual pages and the desktop beside them, the
`desktop`, `catalogue`, `vw` and `please` commands, the root helpers and
the polkit policy, and two PAM files: `/etc/pam.d/hibr`, which the screen
lock checks a password with, and `/etc/pam.d/hibr-login`, the login
screen's.

The packages recommend `pkexec`, which `please`, Control Panel >
Administrative Access, Date & Time, `catalogue --system` and the Login
Manager pane all ask for -- and apt adds a new `Recommends:` only on a
fresh install, so a machine that had hibr before needs `sudo apt-get
install pkexec` once. They conflict with `pleaser`, another program that
installs a `please` command; the Arch package conflicts with `please` as
well.

The desktop in it is what `packaging/ships` lists and what those bring
([ADR 0047](adr/0047-what-the-package-ships.md)): the window manager, the
desktop's own `system/` apps, its own Control Panel panes, the Mystify
screen saver, Files, Terminal and five accessories, and the Classic theme
with the Midnight and Paper colours, in English. The Control Strip ships
with no modules and is off until Control Panel > Control Strip shows it
(0.99.354), and Network, Sound, Bluetooth and Power are catalogue bundles
-- a menu bar item and its pane -- rather than part of the package
(0.99.349, 0.99.351).
There is one package and one route for the rest -- `catalogue install
apps/` places every application in the catalogue, `catalogue install
themes/` every theme. A
second `hibr-desktop-apps` package was built and dropped: it is the
second route again, and `Recommends:` reaches a fresh install only,
never an upgrade, so nobody upgrading would have received it.

Each release is packaged from this tree. `packaging/deb/build-deb.sh` runs
`make install` with `PREFIX=/usr` into a staging directory and packs it with
`dpkg-deb`, depending on the newest glibc symbol version the binary and its
modules actually use. `packaging/deb/publish.sh` adds the package to the
repository (`itdlabs/hibr-apt`, mirrored to GitHub, served by Pages),
regenerates its index with `apt-ftparchive`, signs it with the repository's
own key, and pushes; it fails loudly if the push does. Every version ever
published stays in the pool. `packaging/rpm/build-rpm.sh` and
`packaging/arch/build-pkg.sh` repack that same Debian package for dnf and
pacman, so the three hold the same files byte for byte -- only the PAM
files differ, using each system's own `login` rules -- and each has a
`publish.sh` beside it for its repository (`itdlabs/hibr-rpm`,
`itdlabs/hibr-arch`). The Homebrew formula is refreshed by
`release.sh --formula` and `tools/homebrew-sync.sh`.

## Privileged helpers

hibr is never setuid ([ADR 0016](adr/0016-privileges-are-dropped-never-gained.md)).
Everything the desktop or a person needs done as root -- running a
command for `please`, changing who is an administrator, setting the
clock, installing for every user, changing the login screen -- is a small
hibr script in `/usr/lib/hibr`, run through `pkexec` under an action of
its own: `org.hibr.run`, `org.hibr.access.apply`, `org.hibr.settime`,
`org.hibr.catalogue` and `org.hibr.loginscreen`. Who may use each, and
with whose password, is set in Control Panel > Administrative Access;
[please.md](please.md) has the details. An install anywhere but `/usr`
carries none of this, since polkit reads its actions from
`/usr/share/polkit-1/actions` alone and runs only a helper an action
names.

---

[← documentation index](README.md) · [← project README](../README.md)
