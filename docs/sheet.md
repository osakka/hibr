# Sheet

A spreadsheet whose formula language is hibr.

That is the whole of what makes it different, and it is the one thing you
cannot guess from looking at it. A cell holds text, a number, or **`=` and
then a shell command**; what the command prints is the cell's value.

```text
 B  I  L  C  R  0  0.00  %  1,000  ƒx
ƒx A1│ = then hibr:  =math "A1 * 1.2"   =sum "${B1_B9[@]}"   =$((A1 * 2))
         A         B         C         D         E         F         G
   1
   2
```

The second row is the **formula bar**: which cell you are on, and what is
actually in it — its source, so a formula shows as the formula and not as
its answer. Type in it, or just start typing in a cell. On an empty cell it
shows what a formula looks like, which is the shortest documentation there
is. **Help > How Formulas Work** is the longer version, and **Help > Open
the Tour** is a sheet of worked examples you can change and watch
recompute.

## Formulas

What the command prints is the value:

| you type | the cell shows |
|---|---|
| `=date +%F` | today's date — any command you can run |
| `=math "2 * 21"` | `42`, through the `math` module |
| `=ls -1 \| wc -l` | how many files are in the working directory |

A formula that **begins with an expansion is that expansion**, with no
command run at all:

| you type | the cell shows |
|---|---|
| `=$((A1 * 2))` | arithmetic |
| `="${A1} each"` | text |

## Cells are variables, ranges are arrays

Every cell a formula names is a variable of that name, holding its value;
a range `A1_B9` is an array of the values in it, row by row. So

```text
B8   12.50
B9   4
B10  =math "B8 * B9"        → 50
```

and over a range —

```text
B13  10
B14  22
B15  13
B16  =sum "${B13_B15[@]}"   → 45
B17  =avg "${B13_B15[@]}"   → 15
B18  =max "${B13_B15[@]}"   → 22
```

`sum`, `avg`, `min`, `max` and `count` come from the `math` module, which is
loaded for every formula. Formulas are worked out in the order they need
each other; one that needs itself says `#CYCLE`.

## Nothing a formula does can touch anything

A formula runs in a hibr of its own, **under `--plan`** — the dry run of
`docs/adr/0027`. It can compute and read what you can read, and anything
that would write, connect or start a program that does is refused:

| the cell says | because |
|---|---|
| `#REFUSED` | it would have written, connected or run something that does |
| `#TIME` | it used its two seconds of CPU; a loop says this rather than hanging the desktop |
| `#CYCLE` | it needs its own value |

So a sheet that arrives by mail cannot act when you open it. **Sheet >
Trust This Sheet** lifts the refusal for *that one sheet*, and the trust is
kept on this machine — never in the sheet — so a sheet cannot arrive
trusted. The Tour has a cell that tries to write a file and a cell that
loops, on purpose: they are the only way to see what the sandbox does.

## The Tour

Help > Open the Tour opens a new window and fills it from the example that
ships with the desktop (`examples/tour.csv`). Every formula in it is one
you can click, read in the formula bar, change, and watch work out again:

```text
┌─┤ Sheet [Tour] ├─────────────────────────────────────────────────────────────┐
│ B  I  L  C  R  0  0.00  %  1,000  ƒx                                         │
│ƒx A1│ A sheet whose formulas are hibr                                        │
│         A         B         C         D         E         F         G        │
│   1 A sheet …                                                                │
│   2 Click a …                                                                │
│   3                                                                          │
│   4 Any comm… 2026-10-… today; t…                                            │
│   5 Floating…        42 through …                                            │
│   6                                                                          │
│   7 A cell i…                                                                │
│   8 Price         12.50                                                      │
│   9 Quantity          4                                                      │
│  10 Price ti…        50 B8 and B…                                            │
│  11                                                                          │
│  12 A range …                                                                │
│  13 Jan              10                                                      │
│  14 Feb              22                                                      │
│  15 Mar              13                                                      │
│  16 Their sum        45 B13_B15 …                                            │
│  17 Their av…        15 and the …                                            │
└──────────────────────────────────────────────────────────────────────────────┘
```

It opens as a new, unnamed sheet, so the first Save asks where — it cannot
overwrite the example and it cannot land on what you were working on.

## Getting around

Arrows move; shift with them selects; typing replaces the cell, `enter` or
`f2` edits it, `enter` puts it in and moves down, `tab` right, `escape`
leaves it as it was; `delete` clears what is selected. Drag a column's
right edge in the header to change its width. The **Sheet** menu inserts
and deletes rows and columns — formulas follow the cells they name — and
adds more of both.

## Format

The toolbar is the Format menu's most-used items, and both run the same
code:

| button | what it does |
|---|---|
| `B` `I` | bold, italic |
| `L` `C` `R` | align left, centre, right |
| `0` `0.00` | no decimals, two decimals |
| `%` `1,000` | percent, thousands separators |
| `ƒx` | Help > How Formulas Work |

The menu has more: a text colour and a fill from the theme's own roles, a
currency, a bottom or right border, rows and columns frozen at the top and
left while the rest scroll, and a rule that colours a number by its value
(`< 0 bad`, `> 100 good`). A cell's format is kept beside its text, as
items — `b;i;fg=bad;al=r;nf=2`.

## Files

A sheet is a file of the `db` module (`.hsheet`): one row per cell, written
as it is changed, so **there is nothing to save**. Save As writes a copy
under another name; Edit > Undo undoes this session's changes. CSV comes in
and goes out through File > Import and File > Export — and a CSV field that
begins with `=` arrives as a formula, which is how the Tour is shipped as
text rather than as a binary.

Sheets live in `SS_DIR`, `~/.local/share/hibr/sheets` unless you set it.

## Where this is written down

The app is `examples/desktop/apps/Office/sheet.hibr`, the example is
`examples/desktop/examples/tour.csv`, and the checks are in
`tests/apps.py`. Until 0.99.120 every word of this page lived in that
file's own header comment, where no one using the app would ever find it
(Gitea #150).
