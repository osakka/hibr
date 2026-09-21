# 0013 — Prompt configuration uses the shell's own map model

Status: accepted

## Context

The obvious reference for a segmented prompt is starship, which is configured
by a TOML file. Matching it would mean carrying a TOML parser — several hundred
lines that exist only to read configuration — and committing to another
project's schema, including the parts of it that do not fit.

hibr already has an ordered, nestable map type, and the shell is already the
configuration language: `~/.hibrc` is a script.

## Decision

Prompt configuration lives in a map named `PROMPT`. Top-level settings are
`PROMPT[format]` and `PROMPT[newline]`; everything else is
`PROMPT[segment][key]`. Any key a segment does not handle itself is readable
from its format string as a variable, so `PROMPT[dir][read_only_style]` is
picked up by `$read_only_style` with no code behind it.

The format language — `$var`, `[text](style)`, and `(conditional)` — follows
starship's grammar, so the shape of a format string transfers even though the
configuration file does not.

## Consequences

No new parser, no new file format, and configuration is live: change
`PROMPT[dir][style]` at the prompt and the next prompt shows it. Everything the
shell can already do — conditionals, functions, `$( )` — is available when
writing a prompt configuration, because it is a script.

The cost is that an existing `starship.toml` cannot be dropped in. Keeping the
format grammar compatible means a TOML loader could be added later as a module
without changing anything here, but that is not the same as compatibility
today.

---

[← decisions](README.md)
