# md — markdown, as CommonMark and GitHub write it

`md` parses markdown the way the CommonMark spec defines it, with GitHub's
extensions on top (tables, strikethrough, task lists, extended autolinks and
the tag filter), and gives it back either as HTML or as one style string per
source line, for a program that draws markdown itself. Write, the desktop's
word processor, is built on the second.

```text
mod load md                     # or need md
md html notes.md                # HTML, as cmark-gfm writes it
md html -c notes.md             # plain CommonMark, no extensions
md html -t "$text"              # the document given as an argument
md html < notes.md              # or on standard input (also "-")
h := md html -t '# hi'          # into the result slot, without the last newline
st := md lines notes.md         # an array: one style string per line
```

## How complete it is

Every example in both specs, compared byte for byte with what the spec says
the output is — not normalised — by `tests/md_spec.py`, which runs in
`tests/all.py`:

| suite | examples | pass |
|---|---|---|
| CommonMark 0.31.2, extensions off | 652 | 652 |
| GFM 0.29 extensions (tables, strikethrough, autolinks, task lists, tag filter) | 24 | 24 |

The spec data is vendored in `tests/md/` (both specs CC-BY-SA 4.0: John
MacFarlane's CommonMark and GitHub's GFM). The parser follows cmark's
structure — blocks a line at a time (open containers matched, new blocks
opened, then lazy continuation), then each leaf's inlines with the spec's
delimiter-stack algorithm — so where the spec is silent it behaves as cmark
and cmark-gfm do.

What it does not do: footnotes, which are a later GitHub addition outside
the GFM spec; smart punctuation; and the safe mode that cmark strips raw HTML
and `javascript:` links in — the HTML here is what the document says, as the
spec tests require. GitHub's tag filter is applied, so `<script>`, `<style>`,
`<iframe>` and the rest of its list come out escaped.

## Limits, so input cannot hurt

Containers (quotes, lists) nest at most `MK_DEPTH` deep (500) and inline
links and emphasis likewise; anything past that is text rather than a
deeper tree, so no input can exhaust the stack. Every pathological case cmark
tests for — thousands of nested brackets, unclosed links, delimiter runs,
backtick runs, a million `>` — runs in linear time: a megabyte of any of
them takes a second or two. Link labels are found through a hash table.
Everything is clean under ASan and UBSan, including after fuzzing.

## `md lines`

One string per source line, one letter per *character* (not byte), so
`${style:i:1}` describes `${line:i:1}`. Content is lettered by what it is,
markup by what it is for:

| letter | meaning |
|---|---|
| `p` | paragraph text |
| `q` | text in a block quote |
| `b` `i` `B` | strong, emphasis, both |
| `s` | strikethrough |
| `c` | a code span's text |
| `l` | a link's text, an autolink |
| `g` | an image's description |
| `1`–`6` | a heading's text, by level |
| `f` | a code block's text |
| `h` | raw HTML, block or inline |
| `m` | markup with nothing to show: `**`, a link's `(url)`, a fence, `#`, a setext underline, a backslash |
| ` ` | indentation at the start of a line, and the space after a list marker |
| `-` | a bullet list's marker |
| `n` | an ordered list's number and its `.` or `)` |
| `x` `X` | a task's box, unticked or ticked (all three characters) |
| `>` | a block quote's marker |
| `r` | a thematic break |
| `\|` | a table's column separator |
| `=` | a table's delimiter row |

A program draws the content in its style, hides the `m`s, keeps the
indentation, and draws the structural codes as whatever stands for them —
Write uses a bullet, a box, a bar and rules.

## Files

| file | what it is |
|---|---|
| `mk.h` | the node, the parser's state, every function the files share |
| `blk.c` | blocks: containers, leaves, tabs, lazy continuation, tables, reference definitions |
| `inl.c` | inlines: code spans, autolinks, raw HTML, entities, emphasis and strikethrough, links and images, GFM's extended autolinks |
| `out.c` | HTML as cmark-gfm writes it, and the style letters |
| `util.c` | Unicode classes, entities, escapes, link destinations, titles and labels, HTML tags |
| `ent.c` | the HTML5 named entities, sorted, from the WHATWG list |
| `md.c` | the `md` builtin |
