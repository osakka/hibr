# 0052 — A sheet's formulas are contained, and full access is per sheet

Status: accepted (0.99.427); the Landlock path is not yet run on a kernel
that has it

## Context

Sheet's formulas are hibr (`docs/sheet.md`), and a sheet is a file people
mail each other, so what a formula can do when a sheet is opened is the
whole of its safety. Until 0.99.426 every formula ran under `--plan`,
which ADR 0027 says plainly is a dry run and not a sandbox: it refuses by
name, and the audit (Gitea #432) found four ways past the names --
`connect`, a scratch path ending in `..`, a program copied into the scratch
under a reassuring name, and `sed`'s own `e` and `w` commands. Those are
closed in 0.99.426, and a dry run is still a list someone has to keep
complete.

The owner asked for the other half as well: a sheet that *does* act on the
world, when its owner wants it to -- writes files, starts programs, uses
the network -- "a sheet as a program". So there are two questions, and
they want two answers: what a sheet nobody has said anything about may
do, and how a person says a sheet may do everything.

## Decision

**Formulas run contained.** The `contain` module (`contain exec DIR -- CMD`)
closes every descriptor above 2, contains its own process and execs the
command, which inherits all of it:

- On Linux, **Landlock**: read and execute anywhere, every kind of write
  only inside `DIR` (and `/dev/null`); TCP bind and connect refused where
  the kernel can (ABI 4); signals and abstract sockets scoped where it can
  (ABI 6).
- Beside it on Linux, a **seccomp** filter answering `EACCES` to `socket`,
  `socketpair`, `connect`, `bind`, `ptrace`, `process_vm_readv/writev` and
  io_uring, so a pathname socket the user can reach -- the desktop's own
  control socket, the session bus -- is no way out either; to every call
  that changes a file's mode, owner, times or extended attributes, which
  Landlock does not govern; and, below Landlock ABI 6, `EPERM` to a signal
  sent to any process but the formula's own hibr. `no_new_privs` keeps a
  setuid program from shedding any of it.
- On macOS, the system sandbox, with a profile saying the same.

A formula therefore reads what you can read, computes, runs programs, and
writes only in a fresh folder of its own (`TMPDIR` points there). What
tries anything else gets an error from the kernel, and the cell says
`#REFUSED` with the program's own words beneath the grid.

**What contains a sheet is found by running it.** `contain probe` answers
what the system offers, and Sheet does not take that on trust: once per
desktop it contains a hibr that writes inside its folder and then outside
it, and only when the first write lands and the second does not are the
formulas contained. Anything else -- no module, a kernel built without
Landlock, a rule this build gets wrong -- leaves the formulas a **dry run**
under `--plan`, as before, and the status line says which of the two it
is. A containment that cannot run must never become a sheet whose every
cell fails.

**Full access is per sheet, remembered, and tied to what the sheet holds.**
Sheet > Run with Full Access (and the toolbar's Full Access) lets one
sheet's formulas run as you. It is kept in this machine's settings, never
in the sheet, so a sheet cannot arrive with it; and it is kept as the
sheet's path and a fingerprint of **every cell** -- not only the formulas,
since a formula with full access acts on what the cells it names say, and
`rm -rf "$A1"` is only as safe as A1. Edits made in Sheet move the
fingerprint with them, and Save As carries full access to the new name. A
file changed or replaced outside Sheet no longer matches, runs contained,
and says so. Full access is everything the person can do; there are no
per-capability switches, which the owner chose over a list of rights
nobody would read.

**Control Panel > Sheet** sets what a sheet never seen before does:
**Contained** (the default), **Ask When Opened**, which asks only of a
sheet that has formulas, or **Full Access**, for someone who writes their
own sheets and wants them to act.

## Consequences

- **Reading is not contained.** A formula can show anything you can read
  -- a key, a mailbox -- in its cell. It cannot send it anywhere, write it
  anywhere but its own folder, or hand it to another process, so it stays
  on the screen of the person who opened the sheet. If that sheet is then
  saved and sent on, what it showed goes with it; values are part of a
  sheet.
- **A contained formula cannot signal even its own children** on a kernel
  older than Landlock ABI 6 (Linux 6.12), because seccomp cannot tell a
  child from the desktop by its pid alone: `timeout` and `kill $!` inside a
  formula fail there. Sheet's own two-second CPU limit and ten-second wall
  are enforced from outside the containment, so a formula that loops still
  says `#TIME`.
- **CPU and disk inside its folder are not bounded** beyond the run's own
  limits and the folder being thrown away before every run.
- **The Landlock path has not run on the machine this was built on**: its
  kernel answers `EOPNOTSUPP`, Landlock not being in the host's list of
  security modules. The seccomp filter was run there, under a harness,
  with hibr, `math`, a pipeline, `sort`, `cksum` and `date` working inside
  it and sockets, `chmod`, `touch` and signals refused. The macOS branch is
  compiled there and has not been run. Both are verified on real machines
  before Gitea #432 closes, and until then the probe is what keeps a fault
  in either from reaching a sheet.
- `--plan` stays what ADR 0027 says it is -- a dry run for reading a
  script's intentions -- and is still what a sheet runs under where nothing
  can contain it.
- One fork for the fingerprint when a sheet opens, and one per recalc of a
  sheet with full access; a contained sheet pays nothing for it.
