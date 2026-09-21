# 0001 — Build with tcc, depend on libc and libdl only

Status: accepted

## Context

A shell is the program you reach for when everything else is broken. If it
needs a large toolchain to build, or drags shared libraries behind it, it is
not available in the situations where it is most wanted. Startup time and
resident memory are also paid on every single invocation — a shell is launched
far more often than it is used interactively.

## Decision

Build with `tcc`. Depend on nothing but libc and libdl. Use gcc only for
sanitizers and profiling, never as a requirement.

Everything the shell needs that libc does not provide is written here: the
regular expression engine, the JSON parser, DEFLATE, SHA-1, and git's object
format. `libssl` is an exception in one direction only — it is `dlopen`ed on
first TLS use and never linked, so a shell that never speaks TLS pays nothing
for the capability.

## Consequences

The whole thing compiles in about a second and the binary is roughly 270 KB.
Resident memory at startup sits below dash. There is no dependency to audit, no
version skew, nothing to install first.

The cost is real. Anything the standard library lacks has to be written and
then tested to the standard of the library that was not used — the DEFLATE
decoder was verified against 32,000 real git objects precisely because there is
no zlib to fall back on. tcc's optimiser is weaker than gcc's, so some hot
paths are slower than they would be otherwise. And glibc's `regex.h` cannot be
parsed by tcc at all, which is why `include/re.h` exists.

---

[← decisions](README.md)
