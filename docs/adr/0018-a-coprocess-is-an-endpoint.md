# 0018 — A coprocess is an endpoint like any other

Status: accepted

## Context

bash's `coproc` is a keyword with a shape of its own. It gives back two
descriptors in a magic array, `${NAME[0]}` to read and `${NAME[1]}` to write,
which have to be memorised because the names say nothing. There is effectively
one of them: starting a second while the first lives prints
`warning: execute_coproc: coproc [pid:name] still exists` and loses track. The
descriptors are ordinary low-numbered ones, so a redirection can tread on them,
and they leak into children unless closed by hand.

hibr already has a vocabulary for talking to something that is not a file:
`connect` hands back a descriptor, `accept` hands back a descriptor, and `send`
and `recv` speak to either. A coprocess is the same shape of thing — a
descriptor to write to and a descriptor to read from.

## Decision

`coproc [name] command [args…]` starts a command as a coprocess and records its
ends the way `connect` records a socket. They are named for what they do:

```
worker() { while recv 0 line; do send 1 "got:$line"; done; }
coproc cp worker
send ${cp[out]} hello
recv ${cp[in]} answer
```

`${cp[in]}` is read from, `${cp[out]}` is written to, and `$cp_PID` is the
process. `${cp[0]}` and `${cp[1]}` hold the same two descriptors, so a script
written for bash still reads. Both exist because arrays are maps
([0006](0006-arrays-are-sparse-maps.md)) and a map can be keyed by a word as
easily as by a number.

The command runs through the shell, so a function, a builtin or an external
command all work. As many coprocesses as wanted run at once, each under its own
name.

## Consequences

`send` and `recv` now reach a socket, a TLS session, a pipe and a coprocess
without changing a word, which is what makes the vocabulary worth having. The
descriptors come from `fd_high`, so they sit above 9 and a redirection cannot
tread on one.

bash's block form, `coproc name { … }`, is not accepted; a function is written
instead, and it reads better. That is the cost, and it is the only thing a bash
script has to change.

The oldest coprocess trap is untouched, because no shell can fix it: a command
that block-buffers its output — `tr`, `sed` without `-u`, most of libc's stdio
when it is not writing to a terminal — will sit on a reply until its buffer
fills. A coprocess written as a hibr function does not have the problem, since
`send` writes straight to the descriptor.

---

[← decisions](README.md)
