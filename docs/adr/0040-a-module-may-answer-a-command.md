# 0040 — A module may answer a command, and a setting says from which end

Status: accepted

Follows [0016](0016-privileges-are-dropped-never-gained.md) on where modules
may be loaded from, and the module mechanism itself.

## Context

A module's builtins become commands once it is loaded. Until 0.99.112 getting
one loaded was the user's problem: `need x`, `mod load x`, or a
`command_not_found` function in `~/.hibrc` —

```text
command_not_found() {
	mod find "$1" > /dev/null 2>&1 && "$@" ||
		{ echo "hibr: $1: command not found" >&2; return 127; }
}
```

— which `deploy.sh` wrote and nothing else did. A plain `brew install hibr` or
the apt package never runs `deploy.sh`, so neither wrote that file: a whole
release shipped the autoloader "on by default" for every install except the one
this project's own owner uses, and it was reported as *"`sysinfo` did not
autoload"*. A mechanism whose presence depends on which installer you used is
not a mechanism; and a shell that makes somebody learn `need` before `sysinfo`
works is, in the owner's words, *"forcing people to learn stuff that's
useless"*.

## Decision

`HIBR_MODULES` is one of three words, and the shell itself does the loading.

| value | the command search |
|---|---|
| `off` | alias, function, builtin, PATH. A module needs `need` or `mod load`. |
| `after` (default) | …then a module that declares that builtin, if nothing else answered. |
| `before` | alias, function, builtin, **module**, PATH. |

`--modules=off|after|before` sets it for one invocation. A module already
loaded is already a builtin and shadows PATH whatever this says — that is what
`mod load` has always meant, and this setting governs **autoloading**, not
shadowing.

`after` is the default because it cannot change what any existing script does:
it only fires where the command does not exist, which was an error before.

## Consequences

- `sysinfo`, `most`, `hvi`, `trace`, `img` and the rest work out of a fresh
  install of any kind, with no startup file at all.
- `deploy.sh` stops writing `command_not_found`. An existing `~/.hibrc` that
  still has one is harmless: under `after` the module has already answered
  before anything reaches it.
- `before` needs an **index**, and this is the cost worth naming. Finding which
  module declares a builtin means walking the module path and `dlopen`ing each
  candidate to read its descriptor: 4.3 ms for this machine's 35 modules. Paid
  once per not-found command, as `after` does, that is nothing. Paid before
  PATH on *every* command that is not a builtin or a function — every `ls`,
  `git`, `grep` — it would be ruinous on a shell whose first priority is loop
  throughput. So the first command that reaches it builds a sorted
  name → object table and every later lookup is a `bsearch`. Measured on the
  60,000-iteration builtin loop and on 400 forks, `off` and `after` cost
  nothing at all: the added work is one cached integer and one call per command
  that is neither a builtin nor a function.
- A module **installed after this shell started** is not in that index, so
  `before` will not find it until a new shell; `need` and `mod load` still do.
  The index is deliberately *not* thrown away when a module loads or drops,
  because what is on the path has not changed — only what is in memory, which
  `m_find` already answers.
- A `--plan` run never autoloads. A plan has promised to perform no action it
  cannot account for, and a module's init may do anything; the two builtins
  that can load one (`mod`, `need`) already refuse under a plan.
- Root reads the index from `HIBR_MODDIR` alone, the same rule
  [0016](0016-privileges-are-dropped-never-gained.md) gives every other module
  load.
- An assignment prefix is in force for the shell resolving *that* command, so
  `HIBR_MODULES=before ls` means "the module, for this one command" — which is
  the shape asked for, and is why a test of a bad value sees the warning twice,
  once from each shell.

---

[← decision records](README.md) · [← documentation index](../README.md)
