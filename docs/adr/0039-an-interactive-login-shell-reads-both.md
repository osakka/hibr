# 0039 — An interactive login shell reads the profile *and* the rc file

Status: accepted

## Context

Until 0.99.111 hibr read nothing at login. `rc_load` read `~/.hibrc` when
stdin was a terminal, and that was all — so a hibr set as someone's login
shell (which `deploy.sh` offers to arrange, and `/etc/shells` makes possible)
missed `/etc/profile` entirely: the `PATH` a distribution puts there, the
`umask`, everything in `/etc/profile.d/*.sh`, every setting a system
administrator has made for every shell on the machine.

`-l` / `--login` fixes that, and a leading dash in `argv[0]` counts the same,
because that is the only signal `login`, `getty` and `sshd` give. The files
are `/etc/profile`, then the first of `~/.hibr_profile`, `~/.hibr_login`,
`~/.profile`, and `~/.hibr_logout` on the way out.

That much is bash's shape. The question this record answers is what an
**interactive login** shell reads, where bash and zsh disagree.

## Decision

An interactive login shell reads the profile files **and then `~/.hibrc`**.

bash reads only the profile for a login shell. The consequence is one of the
best known potholes in shell configuration: an alias or a prompt written in
`~/.bashrc` works in a terminal window and silently does not after `ssh host`
or at a virtual console, until someone learns that `~/.bash_profile` has to
source `~/.bashrc` by hand — which is why nearly every distribution ships a
`~/.bash_profile` that does exactly that, and why the ones that forget are a
recurring bug report. zsh reads both (`.zprofile` then `.zshrc`) and has no
such pothole.

hibr follows zsh here. The reasoning is this project's own, from the owner:
*"I prefer this rather than forcing people to learn stuff that's useless."* A
person who writes `~/.hibrc` means "this is how I want my hibr", not "this is
how I want my hibr except when I log in".

## Consequences

- An interactive login shell runs `~/.hibrc` after the profile, so the rc file
  has the last word on anything both set. That is the order a person expects:
  the machine's setup, then mine.
- A **non-interactive** `--login` shell reads the profile and not `~/.hibrc`,
  as bash does — `~/.hibrc` is an interactive file and always has been.
- A `~/.profile` that sources `~/.hibrc` (copied from a bash habit) would read
  it twice. Harmless for the ordinary contents of one, and worth knowing: an
  rc file with a side effect that is not idempotent should guard itself, which
  `[ -n "${MY_RC_DONE-}" ] || ...` does in one line.
- `HIBR_PROFILE` overrides the system profile's path, the way `HIBR_RC`
  already overrides the rc file's. It exists for tests (`tests/999-login.t`
  would otherwise read whatever `/etc/profile` the machine running it has) and
  for a packager or a container that keeps one somewhere else.

---

[← decision records](README.md) · [← documentation index](../README.md)
