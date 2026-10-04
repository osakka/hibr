# 0028 — A theme is data, kept as JSON

Status: accepted

## Context

The desktop's ten themes were a `case` statement inside `cp_theme`, in the
Appearance pane: seven colours and a shadow strength each. Adding a theme
meant editing that file, and a person could not keep one of their own
without replacing the whole pane.

The project's rule is that what the user configures is a script of their
own, not a configuration file format: the session file, apps, panes and the
settings file are all hibr. A theme could follow it -- a file of
assignments, sourced.

## Decision

A theme is a JSON file, one to a theme, named for it:

```text
{
  "wall": "#0d1b2a", "dot": "#16324a", "bar": "#1b3a5c",
  "active": "#63b3ed", "idle": "#4a5568",
  "face": "#101820", "ink": "#cbd5e0",
  "shadow": 55
}
```

The bundled ones are in `examples/desktop/themes/`, installed beside the
desktop; `~/.config/hibr/themes/` is searched first, so a file there with a
bundled theme's name replaces it and a new name adds one. Each file is
checked whole when the Appearance pane loads -- every colour `#rrggbb`, the
shadow a whole number from 1 to 100, `checks` (optional) one of `box`,
`knob` or `block` -- and one that fails is left out of
the list rather than half applied. The list is sorted by name.

## Why not a script

The rule about scripts is about behaviour: what runs at startup, what an
app does, what a session opens. A theme has no behaviour. It is eight
values, closer to a wallpaper image than to a session, and nothing in it
needs computing.

It is also the thing people pass around. A theme written as a script would
run, with the permissions of whoever applied it, inside their desktop -- so
taking a theme from someone else would mean reading it as carefully as a
program. A JSON file can only ever be colours. `json` is a builtin, so
reading one costs nothing extra, and anything that can write JSON can make
one: a converter from another terminal's palette is a few lines in any
language.

Since 0.85 a theme may also set seven colour roles -- dim, selink, good,
warn, bad, info, well -- each optional and validated the same way, each
defaulting to the dark themes' value.

## Consequences

- The order is alphabetical, not chosen: there is no list to choose it in.
  midnight stays the default.
- A theme file added while the desktop runs appears at its next start.
- The settings file keeps the chosen theme's colours as well as its name,
  so removing a theme file never changes how a running setup looks.

## Since 0.99.33: colours, and themes that are the whole look

What this record calls a theme became a **colour scheme**: the same file,
the same checks, moved to `examples/desktop/colours/` and
`~/.config/hibr/colours/` (a colour file left in `~/.config/hibr/themes/`
is still read as one). A **theme** is now the whole look -- a colour
scheme by name and the wallpaper, frame, title bar, buttons, checkboxes,
shadows and glyph set -- and is data for the same reason: it is still the
thing people pass around, and still nothing in it computes. Each value
must be one its setting can take, or the file is left out whole; what a
theme leaves out is the desktop's default. Its picture is the one thing
it names outside itself, a path read by the image module like any
wallpaper chosen by hand. The settings file's `CP_THEME` held the colour
scheme's name until then; settings version 4 moves it to `CP_COLOURS`
once.
