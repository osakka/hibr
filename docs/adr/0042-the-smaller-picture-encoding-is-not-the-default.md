# 0042 — The smaller picture encoding is not the default

Status: accepted

## Context

The kitty graphics protocol takes a picture three ways, and hibr can now
write all three (`mods/deflate.c`, `mods/png.c`, 0.99.128):

| | control | the owner's own wallpaper at 232x71, base64 |
|---|---|---|
| raw pixels | `f=24` | 2.11 MB |
| a zlib stream of those pixels | `f=24,o=z` | 0.65 MB |
| a PNG | `f=100` | 0.47 MB |

Measured through the real path at the owner's own size and detail setting,
not computed: 3.4 seconds against 1.0 against 0.7 on a 5 Mbit/s link
(Gitea #129, reported as *"i just did a desktop -r ... 10/15 seconds-ish,
then the desktop appears"*). It matters because `hold` re-emits every picture
to every client that attaches and every desktop is held, so this is what a
reattach ought to cost.

**Ought to**, because gating this release turned up Gitea #165: a picture
drawn *under* the text — which is what a wallpaper is — reaches a held client
not at all today, measured at zero APCs for a held desktop and for a bare
held program alike, and identically with the compression off. So these are
the bytes a console with a cell size sends; whether they are the bytes the
owner's own reattach carries is that ticket's question, not this one's. The
decision below does not depend on the answer.

The PNG is 28% smaller than the zlib stream. The obvious default is the
small one.

## Decision

**`o=z` is the default. `f=100` is a setting** — `console imgcomp
zlib|png|off`, Control Panel > Pictures > Compress Pictures.

## Why

**A payload a terminal cannot read is a payload it says nothing about.**
Every escape the console sends carries `q=2` — answer nothing, not even an
error — and that is not a preference: a reply arrives in the same stream the
key decoder owns, and its `ESC` lands in the Alt/Escape disambiguation
window, so a terminal that *did* answer would cost a keystroke. With `q=2`
set, a terminal that implements `f=24` and not `f=100` draws a blank
rectangle, the console goes on skipping the cells it thinks the picture
covers, and **nothing anywhere errors**.

That is not hypothetical. It is the shape of the 0.99.72-0.99.75 bug exactly:
`cn_gfx` mapped kitty to sixel, kitty has never drawn a sixel, and for four
releases every picture on the terminal the owner actually uses was a blank
rectangle — found by a person looking at their screen, because no suite could
have caught it. The harness is told what a cell measures and its terminal
model happily records an escape nobody would ever have rendered.

**And the risk is not evenly spread.** `cn_gfx` sends kitty escapes to four
terminals — kitty, ghostty, WezTerm and konsole. The first three implement
the protocol fully. konsole implements part of it. We do not probe (see
`cn_gfx`'s own comment for why a probe was rejected), so there is no way to
know from inside which one is in front of the person.

So the trade is 0.18 MB — about a third of a second on that link — against a
failure mode that is invisible from inside the program, silent to the user
until they notice a rectangle of nothing, and already cost this project four
releases once. `o=z` is part of the base specification that anything drawing
kitty pictures at all understands.

## The second half: only the wallpaper is compressed

Independent of which encoding, and not a setting:

```c
comp = im->over ? cn_imgcomp : CN_COMP_NONE;
```

Under the text is the wallpaper and nothing else. It is placed rarely — the
keep of 0.99.96 makes a repeat placement free — so the encoding is paid
once: 123 ms to deflate that wallpaper, 244 ms as a PNG.

This was first written as "a palette was chosen from this picture, **or** it
is under the text", on the reasoning that a still is placed rarely. The
release gate refuted it: the browser hands its page over on every frame it
draws and the Image Viewer re-places on every zoom, so for two of the three
callers that set that flag "a still" means "a different picture most
frames". A still photograph therefore goes out uncompressed, which is a real
saving given up — and cannot be had until something distinguishes "a picture
that will not change" from "a picture with a palette".

A film's frame is a different picture every frame. The desktop's frame budget
is 0.7 ms (1% of a core at fourteen frames a second), so compressing one
would be fifteen times the whole budget, per frame, to save bytes that cost
nothing on a local terminal — and a film on a remote terminal is not a thing
this protocol can carry at any encoding. `mods/console/image.c` already said
a film's cost is bytes rather than encoding; this keeps that true.

## Consequences

- Anyone who knows their terminal can have the smaller payload, in one
  dropdown or one verb, and `DT_IMGCOMP` keeps it.
- A deflate encoder exists in the tree now and is shared, so anything else
  that wants to compress has it (`def_raw`, `def_zlib`).
- The encoder cannot be tested by "the picture appeared", for the same `q=2`
  reason this decision rests on. `tests/kitgfx.py` sends one picture all
  three ways and asserts the three describe the same pixels — the PNG
  decoded by the suite itself, chunks, CRCs and filters, rather than by a
  library, since our own writer is on the other side of it.
- If a terminal ever grows a way to say what it accepts, this decision is
  the first thing to revisit.

---

[← decision records](README.md)
