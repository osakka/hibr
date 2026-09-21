# 0016 — Privileges are given up, never taken

Status: accepted

## Context

hibr can bind a listening socket and serve it (`listen`), so it can be the
whole of a small daemon. A daemon that binds a port below 1024 has to start as
root, and everything it does afterwards should not be root. That is the ordinary
shape of a service, and until now the shell had no way to express it.

The neighbouring question is whether hibr should ever *gain* privilege: a
setuid hibr binary, or honouring the setuid bit on a hibr script.

## Decision

The `sys` module gains `drop user[:group]`, which gives up root irreversibly.
hibr is never setuid, and never will be.

`drop` resolves the user, replaces the supplementary groups with `initgroups`,
then `setresgid` and `setresuid` so that the real, effective *and saved* ids all
change together. It then checks its own work: the four ids must read back as
asked, and `setuid(0)` must fail. Failing after anything has changed is not
reported and shrugged off — a half-dropped process is worse than either end
state, so `drop` sets `quit` and the shell leaves.

Because a root process must load the module *before* it can drop, `m_open` no
longer searches `.` or `HIBR_MODPATH` when the effective uid is 0; only
`HIBR_MODDIR` is searched. An explicit path still loads, since naming a path is
an informed choice.

## Consequences

The daemon shape is expressible today with `listen -f`, which forks before
running the handler: the handler begins with `drop`, so connections are served
unprivileged, while the accepting parent stays root. That is the inetd
arrangement, and its limit is the same one inetd has — the parent is still root.

A cleaner arrangement, where the parent drops immediately after binding and
keeps only the socket, is not expressible: `net_bind` is reached only from
`b_listen`, which binds and then loops, so there is no seam between the two.
Adding one — a bind that returns the descriptor for the existing `accept`
builtin — would make the parent unprivileged too. It is not done here.

Not being setuid costs nothing, because the alternative never worked. Linux,
like most Unixes, ignores the setuid bit on `#!` scripts entirely (`execve(2)`
says so), so a setuid hibr script was never a thing that ran. A setuid hibr
*binary* is a different proposition and the answer is still no: the module
search consults the environment, `HIBR_TLS_INSECURE` turns off certificate
checking from `getenv`, libssl is `dlopen`ed by name, an interactive shell
sources `.hibrc`, and the prompt hook runs commands between commands. Each of
those is a root shell for whoever controls the environment. The shell is built
to be extended at run time, and that is the opposite of what a setuid program
needs.

So privilege in hibr only ever goes one way.

---

[← decisions](README.md)
