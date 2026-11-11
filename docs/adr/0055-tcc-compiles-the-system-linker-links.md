# ADR 0055: tcc compiles, the system linker links

Status: accepted (owner, 2026-11-11); first step in 0.99.493

## Context

hibr is C built with tcc, and tcc 0.9.27 writes no `PT_GNU_STACK` program
header into anything it links, shell or module, and takes no
`-z noexecstack`. Two things follow, both measured on 2026-11-11:

- glibc records a missing header as an executable stack. A thread a module
  starts (the media module's decode and sound threads) gets an `rwxp`
  stack. On 64-bit Linux since 5.8 the main stack stays `rw-p`.
- glibc 2.41 refuses to `dlopen` a shared object with no header into a
  process whose own stack is not executable ("cannot enable executable
  stack"). A gcc-built process -- the sanitizer build, a distribution that
  rebuilds hibr with its own compiler, Blit loading a tcc-built adapter --
  could no longer load hibr's modules. The sanitizer gate broke on it the
  day glibc 2.41 reached the build machine, and was patched by linking that
  build with `-z execstack`, which made the test shell less like the
  shipped one.

`patchelf --clear-execstack` adds the header to a shared object but
aborts on a tcc-built executable; `execstack` is not packaged in Debian 13;
tinycc's development branch writes no header either.

## Decision

tcc stays the compiler: every file is compiled by tcc, so the rule that
everything compiles under tcc is unchanged and still enforced. The **link**
step uses the system linker when one is present -- the C driver (`cc` or
`gcc`) given tcc's objects, `libtcc1.a`, `-z noexecstack` and RELRO --
through `tools/tccld`, which the Makefile calls in place of tcc and which
is tcc itself where there is no system linker. tcc's `.stab` sections are
made ordinary debug sections first, so `addr2line` still finds a line.

**In two steps**, because a shell linked this way refuses any module built
the old way under glibc 2.41, and two are installed from other
repositories: hibr-browser's `webkit.so` and wiretap's `wiretap.so`. A
module linked this way loads into the old shell and the new alike. So:

1. every module hibr builds (0.99.493: `tools/tccld` links shared objects,
   and the shell is still linked by tcc);
2. those packages rebuild theirs, with `tools/tccld` or with
   `patchelf --clear-execstack` on the `.so`;
3. then the shell (`TCCLD_SHELL=1` tries it before that).

Until the third step a module's threads still get executable stacks, since
the shell's missing header decides that for the whole process.

Every packaging builds the same way (the owner's uniformity rule, the one
that refused the module split). `tests/999-stack.t` reads every module's
program headers -- and the shell's, from the third step -- and fails on a
missing or executable `GNU_STACK`, so a build without the system linker
cannot pass the gate unnoticed. The sanitizer build's `-z execstack` goes.

## Consequences

- hibr's modules load into any process, and no stack is executable once
  the third step is done.
- tcc's in-process `-bt` backtrace is lost; hibr never used it.
- A build machine needs binutils and a C driver for the link; without them
  the build still works and the test says what it lacks.
- The first step leaves the shell's binary untouched. Linking the shell
  this way (prototyped) added 2.7 KB to the stripped binary, under half a
  percent; `tools/measure.py` is run again for the third step.
- Add-ons built in other repositories use the same link through the shared
  packaging pipeline (#506), rather than each remembering it.

## Alternatives

- **Patch only the modules** (`patchelf --clear-execstack` after linking):
  modules load anywhere, but thread stacks stay executable and the shell
  cannot be patched. Half the fix, and the first step of this one without
  the way to the rest.
- **Leave it**: the sanitizer gate keeps its workaround, and any gcc-built
  process on glibc 2.41 cannot load a hibr module.

---

[← documentation index](../README.md) · [← project README](../../README.md)
