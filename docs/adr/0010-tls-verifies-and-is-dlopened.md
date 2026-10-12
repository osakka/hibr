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

## A private authority (0.99.153)

Verification by default left a self-hosted server signed by somebody's own CA
with two ways to be reached and no third: change the machine's trust store,
which is root's decision and every program's business, or turn verification
off for everything. `HIBR_TLS_CAFILE` is the third — a file of certificates
trusted **besides** the system's own, applied after
`SSL_CTX_set_default_verify_paths` with `SSL_CTX_load_verify_locations`, in
the one place a context is made, so it reaches `/dev/tls`, the `connect`
builtin and every module that speaks through the relay.

It is deliberately not documentation around OpenSSL's `SSL_CERT_FILE`, which
that call already reads: that variable **replaces** the default CA file, so on
a machine whose public roots live only in that file — Homebrew's openssl@3,
whose default CA directory is empty — naming a private authority silently
stops every public certificate verifying. Added and substituted are different
things, and only one of them is safe to suggest.

Empty counts as unset, which `HIBR_TLS_INSECURE` does not: anything set at all
turns verification off there. Repeating that reading in a second variable
would have been a trap rather than a convention.

---

[← decisions](README.md)
