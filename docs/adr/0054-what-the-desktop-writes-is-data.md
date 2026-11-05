# 0054 — What the desktop writes for itself is data

Status: accepted (0.99.445)

## Context

The project's rule was that what a person configures is a script, not a
format, and the desktop's settings file and Files' per-folder memory were
written as scripts and sourced. ADR 0028 kept themes out of that rule because
a script runs as whoever applies it, and left these two inside it.

That was wrong in practice. Files wrote a folder's path inside double quotes,
so a folder named `$(command)` ran the command at the next start (0.99.380 to
0.99.443). A value in a sourced file is one quoting mistake from being code,
and folder names, senders and servers are not ours to trust. The owner's
ruling: running settings as code is not OK.

## Decision

**What a person writes on purpose is a script: a session file, an app. What
the desktop writes for itself is data, and is never sourced.** Settings are
`desktop.json`, Files' memory `files-views.json`.

**A value reaches only a name that was kept.** `dt_load` assigns `settings`
entries to names in `DT_KEEP`, and `dt_keep` assigns one registered later, so
the file cannot set `PATH` or anything else nobody kept. A name no loaded
file has kept yet is written back as it came.

**A file that does not parse is set aside** as `.bad` and the desktop starts
on its defaults, so nothing overwrites what could still be repaired.

**No migrations.** `DT_SETVER`, its ladder and the upgrade offers are gone: the
product carries no legacy unless the owner asks for it. A saved value keeps
what it says, so a changed default does not reach a file that holds the old
one. Moving existing settings was a one-off, done by a throwaway conversion
outside the repository.

## Alternatives

**The db module.** Fixed-width typed columns and flat rows, for millions of
measurements: settings are about a hundred mixed names and several maps.

**Sourcing the old file once to convert it.** One more run as code, of
exactly the thing being removed.
