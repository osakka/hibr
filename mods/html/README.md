# html — HTML as browsers parse it, queried and laid out as cells

`html` parses HTML the way the WHATWG HTML standard says a browser must:
its own tokenizer and tree builder, every insertion mode, the adoption
agency for misnested formatting, foreign content (SVG and MathML), and
template contents. The tree stays in memory behind a handle, and a script
can ask it things — the title, CSS selector matches, an element's text or
attributes — or lay it out as lines of text for a cell display, with a
style letter per character, link columns and boxes for pictures. Mail, the
desktop's mail app, reads every HTML message through it.

```text
mod load html                        # or need html
html dump page.html                  # the tree, as html5lib's tests print it
html dump -f td -t '<b>x</b>y'       # a fragment, parsed in a context element
h := html parse page.html            # a handle: a file, -t text, or standard input
html title "$h"
n := html query "$h" 'ul > li'       # node numbers, document order
html text "$h" "${n[0]}"             # an element's text, whitespace collapsed
html attr "$h" "${n[0]}" class       # one attribute; without a name, all of them
html tag|kids|parent "$h" [node]     # what a node is, its children, its parent
l := html lines "$h" 72 [-i] [-a] [node]
html close "$h"
```

## How complete it is

`tests/html_tree.py` runs html5lib's tree-construction suite, vendored in
`tests/html/` (MIT), and compares the printed tree byte for byte:

| suite | cases | pass |
|---|---|---|
| html5lib tree construction, every `.dat` file | 1792 | 1792 |

That includes the current standard's `select` rules — `select` is no longer
a mode of its own, and `<selectedcontent>` is filled from the chosen option
— which older parsers get differently. Named character references come from
WHATWG's `entities.json`, generated into `ent.c` by `tools/htmlents.py`.

What it does not do: run scripts, apply style sheets, or fetch anything.
A document is parsed as the standard says to with scripting off, so what is
inside `<noscript>` is ordinary markup; `-s` parses as a browser running
scripts would, with `<noscript>` as raw text. `html lines` honours the
`hidden` attribute and an inline `display:none`, which is what keeps a
newsletter's preheader out of view, and nothing else of CSS.

## Queries

`html query h selector [node]` answers node numbers in document order,
searching under `node` when one is given. Selectors are CSS's:

| form | example |
|---|---|
| type, `*`, `#id`, `.class` | `div.note`, `#main` |
| attribute | `[href]`, `[lang=en]`, `[lang\|=en]`, `[data-k^=v]`, `[src$=".png"]`, `[title*=x]`, `[class~=a]`, `[type=a i]` |
| combinators | `a b`, `a > b`, `a + b`, `a ~ b` |
| pseudo-classes | `:first-child`, `:last-child`, `:only-child`, `:first-of-type`, `:last-of-type`, `:nth-child(2n+1)`, `:nth-last-child(odd)`, `:empty`, `:root`, `:not(...)` |
| lists | `h1, h2` |

```text
$ h := html parse -t '<div><p lang=en-GB>1<p>2<p data-k=v>3</div>'
$ html query "$h" 'p[data-k^=v], p:first-child'
5
9
$ n := html query "$h" 'p[data-k^=v], p:first-child'
$ for i in "${n[@]}"; do html text "$h" $i; done
1
3
```

## `html lines`

The tree laid out at a width, one entry a line: `l[i]["t"]` the text,
`l[i]["s"]` a style letter per *character*, `l[i]["a"]` the links on the
line (`column<TAB>length<TAB>address`, a line each) and, with `-i`,
`l[i]["img"]` an image box starting on that line
(`rows<TAB>columns<TAB>src`). `-a` draws rules, bullets and quote bars in
ASCII. Without `:=` it prints the text.

| letter | meaning |
|---|---|
| `p` | plain text |
| `b`, `i`, `B`, `u`, `s` | bold, italic, both, underlined, struck or small |
| `c` | code |
| `l` | a link |
| `1`…`6` | a heading of that level |
| `q` | quoted text |
| `m` | a marker the layout drew: a bullet, a quote bar, a table rule |
| `h` | a horizontal rule |

Run on a small page:

```text
Hello there, see the docs.    ppppppbbbbbppppppllllllllp

• one                         mmppp
• two                         mmppp

a  b                          pppp
```

A data table — one with headers, or rows of short cells — is drawn as a
grid with its columns sized to what they hold; a table used for layout,
which is most HTML email, is read row by row instead. An image only one to
three pixels wide or high is a tracking pixel and is left out, with or
without `-i`; without `-i` a picture is its `alt` text.

## Limits, so input cannot hurt

The open-element stack is capped at `HL_DEPTH` (512): past it, an element
is a sibling instead of a child, so no document can exhaust the stack or
make a step cost the depth of the tree. Element kinds the tree builder asks
about on every token (special, the scope boundaries, list and button
scope) are flags worked out once per element, not name comparisons.
Everything is clean under ASan and UBSan.

Measured on an 813 KB generated page of 6000 sections (gcc build of the
shell, tcc build of the module): `html parse` 159 ms, a selector query
over the whole tree 7 ms, `html lines` at 100 columns 0.25 s printed. Bound
with `:=`, the same 30,000 lines took 45 s, all of it in the shell's maps,
which are lists: a result that large is quadratic to store. An email is a
few hundred lines and costs milliseconds; a whole book bound into `$RET`
does not.

## Files

| file | role |
|---|---|
| `html.c` | the builtin, handles, and the subcommands |
| `tok.c` | the tokenizer: every state of the standard's, character references |
| `tree.c` | the tree builder's machinery: the stack, scopes, active formatting, adoption |
| `body.c` | the insertion modes |
| `dom.c` | nodes, attributes, text, and the html5lib dump |
| `query.c` | the CSS selector engine |
| `lay.c` | `html lines`: blocks, inline runs, lists, quotes, tables, images |
| `ent.c` | the named character references, generated |
| `hl.h`, `tr.h` | types shared by the files |
