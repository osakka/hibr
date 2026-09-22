# Deployment

`make install` is enough if you only want the binary somewhere. `deploy.sh`
does the whole job: it builds with the right module directory, runs the test
suite, keeps the previous installation so you can go back, verifies that the
copy it just installed actually works, and can keep itself current.

```
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
2. Runs every suite and stops if anything fails: `tests/run.sh`, then
   `tests/self.hibr`, then the pseudo-terminal suites — `tests/editor.py`,
   `tests/console.py`, `tests/cat.py`, `tests/most.py` — which make their own terminals and so
   run headless, and are skipped with a note where there is no `python3`.
   Nothing is installed from a tree that does not pass. This is checked rather
   than assumed: a deliberately failing test leaves the installed binary
   byte-identical and exits 1, which is what makes the timer report a failure
   instead of quietly doing nothing.
3. Copies the current installation aside, so `rollback` has somewhere to go.
4. Installs, and records a manifest next to the modules: version, a checksum
   of every source file, where it was built from, and when.
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
prompt, screen, cat and trace modules do, would otherwise change without the
update noticing. If the tree is a git checkout with
a remote, it fast-forwards first. Then it repeats the sequence above, tests
included, so an update can never install a tree that fails its own tests.

`auto on` installs a systemd user timer, or a cron entry if there is no user
systemd session, running `update` on the schedule you name — `hourly`, `daily`
or `weekly`.

```
systemctl --user list-timers hibr-update.timer
journalctl --user -u hibr-update.service
```

One caveat worth knowing: a timer runs as you, so if the prefix needs root the
update needs `sudo` without a password. `deploy.sh auto on` says so at the time
if that is not the case. Deploying somewhere you own avoids the question
entirely:

```
./deploy.sh --prefix "$HOME/.local"
```

## Going back

`rollback` restores the binary, the modules and the manifest from the copy made
during the last install, then runs the same checks against the restored copy.
It is one version deep, which is the version you care about when an update has
just gone wrong.

---

[← documentation index](README.md) · [← project README](../README.md)
