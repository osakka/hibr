# 0010 — TLS verifies by default, and libssl is loaded on demand

Status: accepted

## Context

A shell that can open TLS sockets is useful. A shell that links libssl pays for
it in every invocation — around 1.6 MB of resident memory in every shell on the
system, including the thousands that never open a socket. And a TLS
implementation that does not verify certificates by default is a trap, because
the failure is invisible until it matters.

## Decision

Certificates and hostnames are verified by default; `HIBR_TLS_INSECURE=1`
turns that off explicitly. libssl is `dlopen`ed on the first TLS connection and
never linked, and no OpenSSL headers are needed to build. `make TLS=0` compiles
the capability out entirely.

The handshake runs in a relay process that hands back an ordinary file
descriptor, so `read`, `>&3` and redirection work on a TLS socket unchanged.

## Consequences

A shell that never speaks TLS costs nothing for the feature. A shell that does
gets verification without asking, and getting it wrong requires saying so.
Sockets behave like file descriptors everywhere, because they are.

The cost is a process per TLS connection rather than in-process crypto, and a
runtime dependency that is discovered at first use rather than at link time —
so a missing libssl is an error when you connect, not when you start.

---

[← decisions](README.md)
