# 0021 — `${#s}` and `${s:i:n}` count characters, not bytes

Status: accepted

## Context

A window manager written in hibr draws a border by repeating `─` until it is
as wide as the window, then trimming:

```sh
while [ ${#o} -lt "$n" ]; do o="$o─"; done
ret "${o:0:$n}"
```

That produced a border nine characters long with a broken byte on the end,
because `${#o}` was counting bytes — `─` is three of them — and `${o:0:$n}`
was cutting the string in the middle of a sequence.

bash's answer depends on the locale. Under `LC_ALL=C.UTF-8` it counts
characters and the loop above is right; under `LC_ALL=C` it counts bytes and
the loop above is wrong in exactly the way hibr was.

## Decision

**hibr always counts characters.** There is no locale switch, because there is
no locale machinery anywhere else in hibr either, and the rest of the shell has
already decided this question: the line editor moves the cursor over
characters, the console writes a wide glyph into two cells, and `u8dec`/`u8w`
are in the core for both. `${#s}` disagreeing with what the editor and the
display already do would be the real inconsistency.

The whole family was brought into line at once, since a length that disagrees
with a slice is worse than either being wrong:

| | counts |
|---|---|
| `${#s}`, `${s:i:n}` | characters |
| `str len`, `str slice`, `str index` | characters |
| `str pad` | **display columns** |
| `str width` (new) | display columns |

`str pad` is the exception on purpose. Padding exists to line columns up, and
`漢字` is two characters but four columns wide; padding it to eight by
character count would leave the column ragged, which is the one thing padding
is for. `str width` exposes the same measure, so a script can align without
guessing.

## What this costs

Three things, all stated plainly:

- **A `${#s}` in a tight loop costs about 0.8% more**, measured in instructions
  on a 120k-iteration loop. `strlen` is hand-written assembly; the character
  count is a byte loop compiled by tcc, which does not vectorise. Nothing came
  free here and nothing was going to.
- **Against a C-locale bash, `${#s}` on non-ASCII text now differs.** Against a
  UTF-8 bash it agrees, which is what almost every real system has.
- **Malformed UTF-8 counts as one character per stray byte**, since the count
  is of bytes that are not continuation bytes. Nothing rejects bad input; it is
  measured, not validated.

## Alternatives

**Follow the locale like bash.** That means `setlocale`, `mbrlen`, and a
dependency on the locale being installed — and it would make `${#s}` answer
differently on two machines running the same script. hibr has spent its whole
design avoiding that kind of ambient state.

**Leave it byte-based and document it.** This was the state that produced the
broken border. A shell whose editor and display are character-aware and whose
`${#}` is not will keep generating this bug in every script that draws.

---

[← decisions](README.md)
