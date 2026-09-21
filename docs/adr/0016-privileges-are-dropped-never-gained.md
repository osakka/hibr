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

There are two daemon shapes, and both work.

`listen -f` forks before running its handler, so the handler can begin with
`drop`: connections are served unprivileged while the accepting parent stays
root. That is the inetd arrangement, with the same limit inetd has.

`listen -b port [var]` binds without serving and hands back the descriptor, so
nothing has to stay root at all:

```
listen -b 80 LFD
drop www-data
while accept $LFD C; do serve <&$C >&$C; exec {C}<&-; done
```

The socket outlives the privilege that opened it, which is the whole point —
the port was the only thing root was needed for. `net_bind` already returned a
descriptor above 9, so a redirection cannot tread on it, and `accept` already
took a listening descriptor; the seam was the only missing piece.

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
