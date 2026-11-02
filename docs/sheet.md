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

They are worked out as a desktop job, away from the frame loop, so the
desktop goes on drawing and taking keys while a slow formula runs; a formula
cell is blank until its answer arrives. An edit made meanwhile is worked out
once the job ends, and the answer of the run before it is not used.

## A formula is contained until you say otherwise

A formula runs in a hibr of its own, **contained** (`docs/adr/0052`): it
can read what you can read, compute and run programs, but it writes only
into a folder of its own that is emptied before every run, opens no socket,
cannot change a file's mode, owner or times, and cannot signal anything
but itself. The kernel refuses the rest — Landlock and a seccomp filter on
Linux, the system sandbox on macOS — so this holds for a program a formula
starts as much as for the formula.

Where nothing can contain it — a kernel built without Landlock, for one —
a formula runs as a **dry run** instead, under `--plan` (`docs/adr/0027`),
which refuses by name whatever would write, connect or start a program that
does. Sheet finds out which by trying once, and the status line under the
grid says `contained` or `dry run`.

| the cell says | because |
|---|---|
| `#REFUSED` | it tried to write outside its folder, connect, or run something that does; what it said is shown under the grid |
| `#TIME` | it used its two seconds of CPU, or the formulas together ran past ten seconds; a loop says this rather than hanging the desktop |
| `#ERR` | the formula failed, or did not finish; why is shown under the grid when the cell is selected |
| `#CYCLE` | it needs its own value |

Reading is not contained: a formula can show anything you can read in its
cell. It cannot send it anywhere, so it stays on your screen — but it is a
value of the sheet like any other, so a sheet saved and passed on carries
it.

On a kernel older than Linux 6.12 a contained formula cannot signal its
own child either, so `timeout` and `kill $!` inside one fail there.

## Full access: a sheet as a program

So a sheet that arrives by mail cannot act when you open it. A sheet you
want to act — to write its results to a file, fetch a price, start a
build — needs **Sheet > Run with Full Access** (or **Full Access** on the
toolbar). Its formulas then run as you, with nothing refused, and the
status line says `full access`. Choose it again to contain the sheet
once more.

Full access is kept on this machine, never in the sheet, so a sheet cannot
arrive with it. It is kept for the file *as it is*: a fingerprint of every
cell goes with it, which your own edits in Sheet move along with them and
Save As carries to the new name. A file changed or replaced outside Sheet
no longer matches, so its formulas run contained and Sheet says so; give
it full access again if the change was yours.

**Control Panel > Sheet > Formulas in a New Sheet** sets what a sheet
never seen before does: **Contained** (the default), **Ask When Opened**,
which asks only of a sheet that has formulas, or **Full Access**, for
sheets you write yourself and want to act.

The Tour has a cell that tries to write a file and a cell that loops, on
purpose: they are the only way to see what containment does.

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
and goes out through File > Import CSV… and File > Export CSV… — and a CSV field that
begins with `=` arrives as a formula, which is how the Tour is shipped as
text rather than as a binary.

Sheets live in `SS_DIR`, `~/.local/share/hibr/sheets` unless you set it.

## Where this is written down

Sheet does not ship with the package: it is in the catalogue
(`catalogue install sheet`, or the Catalogue window), and once installed
it is under Applications > Office. The app is
`programs/desktop/apps/Office/sheet.hibr`, the example is
`programs/desktop/examples/tour.csv`, and the checks are in
`tests/apps.py`. Until 0.99.120 every word of this page lived in that
file's own header comment, where no one using the app would ever find it
(Gitea #150).
