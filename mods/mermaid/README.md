# mods/mermaid

Mermaid diagrams, drawn into cells.

    mermaid render diagram.mmd        # the drawing, as text
    mermaid render -a diagram.mmd     # drawn out of ascii alone
    mermaid parse diagram.mmd         # the graph, a line an item
    d := mermaid render -t "$src"     # the drawing, with a style per character

## What it understands, and what it says it does not

Mermaid is a family of languages rather than one, so this understands three
of them and **says so plainly** about everything else rather than drawing
something wrong:

| kind | what is accepted |
|---|---|
| `flowchart` / `graph` | `TD` `TB` `LR` `BT` `RL`; nodes `A`, `A[text]`, `A(text)`, `A{text}`, `A((text))`; links `-->` `---` `-.->` `==>` `--x` `--o`, each with `\|label\|` or a label inside it (`-- yes -->`); chains (`A --> B --> C`) and `&` lists; `style`, `classDef`, `class`, `click` and `linkStyle` lines are skipped |
| `sequenceDiagram` | `participant`/`actor`, with `as`; `->>` `-->>` `->` `-->` `-x` `--x`; `Note over\|left of\|right of`; `activate`, `deactivate` and `autonumber` accepted and ignored |
| `pie` | `pie [showData]`, `title` on its own line or on the header, `"label" : value` |

A bad line fails the **whole** diagram, with the line number and what was
wrong, because Mermaid errors whole and half a diagram is wronger than
none:

```text
mermaid: line 2: subgraph is not understood yet
mermaid: line 2: this node shape is not understood yet: [[
mermaid: line 1: this kind of diagram is not understood yet: classDiagram
```

Not understood yet, each named rather than half-drawn: `subgraph`; the
shapes `[[ ]]`, `[( )]`, `[/ /]`, `[\ \]`; sequence blocks (`loop`, `alt`,
`opt`, `par`, `critical`, `break`); and every other kind of diagram
(`classDiagram`, `erDiagram`, `stateDiagram`, `gantt`, `journey`,
`gitGraph`). An edge from a node to itself is dropped with a word about it
and the rest of the diagram still draws.

## With a result slot, the colours as data

`d := mermaid render` prints nothing and fills the slot instead, the shape
`md lines` and `sysinfo` already answer in:

| | |
|---|---|
| `d["kind"]` | `flowchart`, `sequence`, `pie`, or empty when it was not understood |
| `d["why"]` | why, when `kind` is empty |
| `d["note"]` | something left out of a diagram that still drew |
| `d["w"]` `d["h"]` | the drawing's own size in cells |
| `d["text"][i]` | row *i* |
| `d["runs"][i]` | the style of every character in it |

A run list is `b:3 t:9 b:3` — a letter and how many characters it covers,
the whole row covered exactly, so a reader walks the runs and never a
character. The letters: `b` a box's border, `t` a node's label, `e` a line
or an arrowhead, `l` a label on an edge or a message, `k` a title or a
lifeline, `.` nothing.

That is the only way to get these colours into a window. A window draws
cells through its pane rather than bytes at the screen, so an escape
sequence would be no use to it, and `$(mermaid render)` is a pipe. The
module names parts and the caller picks its own theme's colours for them:
`examples/desktop/lib/diagram.hibr` holds the desktop's own mapping, which
the Diagram editor and Write both draw through `widgets/putruns.hibr`.

Counted in **characters**, which is what a shell slice counts, where the
box widths are in **columns**. Every glyph this module draws is one column
wide, so the two agree for anything it drew; a label with a wide glyph in
it makes a row that has more columns than characters, which the widget
notices and measures rather than assuming.

## The layout

A flowchart is the only kind with layout to do, and it is Sugiyama's, the
three steps dagre takes:

1. **Break the cycles.** A depth-first walk reverses the edges that close
   one, so what is left can be ranked. Which edges those are depends on
   where the walk starts, as it does in dagre; the arrow is drawn back at
   the end the text pointed it at, so a loop still reads correctly.
2. **Rank, then bend.** Each node sits one rank below the lowest thing
   that points at it (longest path), and an edge spanning more than one
   rank is split into a chain through a bend point on each rank between --
   which is what lets everything after this assume an edge joins
   neighbouring ranks.
3. **Order and place.** Within a rank, the median heuristic, four passes
   down and up; then each node packed in order and nudged so a parent sits
   over the middle of its children. Exact crossing minimisation is NP-hard
   and nothing here needs it.

Then every gutter between two ranks gets as many tracks as its edges need:
the horizontal runs are coloured greedily, widest first, so **two edges
never share a track**. Without that they merge into one line, which is a
diagram that looks right and is wrong.

`LR` is the same layout read sideways and `BT`/`RL` are `TD`/`LR` with the
level measured from the far side, so there is one layout engine and not
four: everything works in two abstract axes, along the ranks and across
them, and exactly one function says which is which.

The ordering and placing are O(n²) per rank per pass, which is nothing for
a diagram a person wrote by hand and would matter for a generated one of
thousands of nodes. Nobody has one.

## Checking it

Mermaid has no conformance suite to run against, so there are two halves:

- `tests/999-mermaid.t` **records** what a corpus of diagrams
  (`tests/mermaid/*.mmd`) draws, cell for cell, plus every message about
  something not understood. Re-record it on a deliberate layout change,
  after reading the diff -- the geometry in `mermaid parse`'s own output
  moves whenever the gap or the centring does, and that is the recording
  working rather than a regression.
- `tests/mermaid.py` asserts the **properties** a drawing must have
  whatever it looks like: no two boxes overlap, every edge ends on a box it
  names, every run list covers its row exactly, the drawing is the size it
  says it is, every style letter is one the contract names, and `-a` uses
  none of this module's own glyphs. Those survive the drawing being
  improved; the recording does not.

## Files

| file | what it is |
|---|---|
| `mm.h` | the diagram, its nodes, edges, messages and slices, and the glyph names |
| `parse.c` | the three grammars, and everything it says it cannot read |
| `layout.c` | ranks, bend points, ordering, placing, and the gutter tracks |
| `draw.c` | the laid-out diagram into cells, with a style for each one |
| `mermaid.c` | the `mermaid` builtin: rows and runs, or the graph as text |
