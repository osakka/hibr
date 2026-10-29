# please

`please` runs one command as root, or as another user. It does sudo's job
through **polkit**: `pkexec` runs hibr's own helper under hibr's action
`org.hibr.run`. Who may run it, and with whose password, is set in Control
Panel > Administrative Access, beside every other action a package asks
polkit about. There is no sudoers file to edit. hibr itself is never setuid
([ADR 0016](adr/0016-privileges-are-dropped-never-gained.md)); `pkexec` is the
one that is.

<!-- not run: needs pkexec and an administrator's password -->
```sh
please apt-get install pkexec
please -u backup rsync -a /srv/ /mnt/backup/
please -s
```

- `please COMMAND [ARGS...]` runs the command as root.
- `please -u NAME COMMAND ...` runs it as NAME.
- `please -s` opens a shell, root's or NAME's with `-u`, as a login shell.

The command runs in the folder you are in, with your terminal's `TERM` and
your `LANG`. Everything else is pkexec's clean environment, with the
system's own `PATH`. A folder the user cannot enter is replaced by their home
folder, then by `/`. Its status is the command's own, 126 when the password
was refused or cancelled, and 127 when it could not be run at all.

**On macOS**, which has no polkit, `please` is `sudo` underneath: `please
COMMAND` is `sudo -- COMMAND`, `-u NAME` is sudo's own, and `please -s` is
`sudo -i`. sudoers decides who may there, and the folder, the environment
and the status are sudo's. The same word works on every platform, which is
why hibr's own messages and pages say `please` and never `sudo`.

`please` is the general way. Where hibr has a narrower job to do as root it
has an action of its own, so a group can be let do that one thing without
being given root: Date & Time has `org.hibr.settime`, an install for
every user (`catalogue --system`, see [catalogue.md](catalogue.md)) has
`org.hibr.catalogue`, which the catalogue asks for by itself, and Control
Panel > Login Manager has `org.hibr.loginscreen`.

## Who may, and with whose password

By default `org.hibr.run` asks for an **administrator's password and
remembers it for a few minutes**, as sudo does. Control Panel >
Administrative Access lists it as *Run a command as another user*, under
hibr, and offers the same choices as every other action:

| Choice | What it means |
|---|---|
| Package Default | an administrator's password, remembered |
| Administrator's Password | an administrator's password each time |
| Administrator's, Remembered | the default, stated outright |
| Your Own Password | your own password, each time |
| Your Own, Remembered | your own password, remembered a few minutes |
| No Password for a Group | members of one group run it with no password: sudo's `NOPASSWD` |
| Never | refused, for everyone |

The pane writes these choices as one polkit rules file and asks for an
administrator's password to do it. It never offers a choice for its own
action, so the choices made there can never lock you out of the pane.

## What it needs

`pkexec`, which Debian ships in a package of its own since polkit 122. The
hibr packages recommend it, but apt adds a new recommendation only on a
fresh install, so an existing machine needs:

<!-- not run: changes the machine -->
```sh
sudo apt-get install pkexec
```

The helper (`/usr/lib/hibr/hibr-run`) and the policy naming it
(`/usr/share/polkit-1/actions/org.hibr.policy`) come with the packages, and
with any `make install` to `/usr` on Linux. `please` says plainly when either
is missing.

The name is shared with an unrelated sudo alternative written in Rust,
packaged as `pleaser` on Debian, Ubuntu, Fedora and openSUSE, which also
installs `/usr/bin/please`. The hibr packages declare a conflict with it, so
a package manager offers to replace one with the other rather than failing
on the file. The two cannot be installed together.
