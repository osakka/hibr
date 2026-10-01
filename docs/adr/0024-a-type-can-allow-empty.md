# 0024 — A type can allow empty

Status: accepted

## Context

A declared parameter's type is checked on every call: `fn at(int col)`
refuses `col=x`, and refuses an empty `col` as well, since the empty string
is not an integer. That was right for the parameters that were typed, and it
was what kept the desktop's 400 declared functions untyped when they were
converted in 0.44. A census of every call across the suites later found 558
parameters that were an integer every time -- but 30 of them are optional,
written `col = ""`, and 4 take an integer or nothing on purpose (a window
with no fixed size, a menu with no parent). `int col = ""` could not even be
declared, because the empty default itself failed the check the first time
the argument was left out.

Three ways out were considered:

- not checking a default, only what a caller passes -- which makes
  `int col = ""` work when the argument is omitted, but still refuses a
  caller that passes an empty value on purpose, so the four stay untyped;
- typing only the required parameters and leaving the rest as they were;
- a way to say, at the declaration, that empty is allowed.

## Decision

A type may end in `?`: `int? col = ""` accepts an integer or the empty
string, and nothing else. It works for every type -- `num?`, `path?` -- and
for a return type, `-> int?`. Without the `?` nothing changes: `int` still
refuses empty.

```sh
fn at(int? col = "", int row = 1) { echo "col=[$col] row=$row"; }
at            # col empty, row 1
at 7 2
at "" 3       # an empty col, passed on purpose
at x          # refused: status 2, the body does not run
echo "status $?"
```

```output
col=[] row=1
col=[7] row=2
col=[] row=3
hibr: at: col expects int?, got 'x'
status 2
```

The `?` is the one mark other languages use for the same idea, and it puts
the promise where a reader looks for it: in the signature, not in a rule
about defaults that has to be remembered.

## Consequences

The desktop is typed: 528 parameters are `int` and 34 are `int?`, in the 299
functions the suites call, chosen from the census (`tests/census.py
--types`) and cross-checked against every literal call site in the source.
The 145 functions no suite calls are left untyped, because the census has
nothing to say about them -- the lesson of 0.44.1, where a contract taken
only from observed calls failed the calls nobody had observed.

A check costs something on every call, so it was made cheap. The type is
read once, when the signature is parsed, into a few bits on the parameter's
node; the call then checks the value and nothing else. A typed integer
parameter costs about 100 instructions per call, most of it the digit loop
over the value -- down from 428 when each call compared the type's name
against every type there is.

`?` after a type word is new syntax in a signature, so a module or script
that parses signatures itself has one more character to allow.

---

[← decisions](README.md)
