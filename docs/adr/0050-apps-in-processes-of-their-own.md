# 0050 — Apps in processes of their own

Status: proposed (0.99.400), to be decided with the owner

## Context

ADR 0049 makes a slow callback caught three times -- lint, the gate, the log
and the pause -- and says plainly what it cannot do: the first slow run
still stops every display, because every app runs in the desktop's own
process. X has the same rule for its server and a different answer for its
clients: each is a process of its own, so a client that hangs hangs only
its own windows.

The desktop cannot do that today, for one reason recorded in CLAUDE.md:
a window draws into the one grid the console owns, through
`console put -p`, and a pane has no cells of its own. Giving each pane its
own cells and composing them at flush was built once and worked -- until
the picture modules, which write at absolute coordinates (`img draw`, and
media, web and the pager through it), could not be placed in either order
against a pane's own chrome. `dp_api` has no paned put for a picture.

## Proposal

In three steps, each a release that stands on its own:

1. **Panes own their cells.** `dp_api` gains a paned put and a paned image,
   img, media, web and most move onto them, and the console composes panes
   at flush, bottom to top. Nothing outside the console and those four
   modules changes. Measured against today before it is kept: a frame with
   nothing changed must not cost more.
2. **A pane can be drawn from another process.** The console offers a pane's
   cells over a socket -- a write of cells, as `hold` already carries cells
   between processes -- so an app can draw its window from a process of its
   own. The window manager keeps focus, stacking, the menus and input, and
   hands an app its keys and clicks as messages.
3. **Apps move out one at a time**, starting with the ones that do the most
   work (the Browser, YouTube, Task Manager, Files), each a child of the
   desktop that a hang, a crash or a slow callback takes down alone. An app
   in a process of its own needs no budget, no pause and no lint rule: what
   it does with its own time is its own.

## Costs, said plainly

- Memory: a hibr process per app, about 2 MB resident each.
- Latency: a key reaches an app as a message and its window comes back as
  cells, two hops where today there are none.
- Work: `dp_api` and four modules, then a protocol, then each app.
- What stays in-process stays fast; an app moved out cannot be cheaper to
  draw than it is now, only unable to stop anyone else.

## Status

Proposed. Nothing here is built until the owner has decided whether the
robustness is worth those costs, and in what order.
