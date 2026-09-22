# mods/hvi

Modal, and actually vi — but the arrow keys work, undo goes back more than
once, and UTF-8 is a character rather than a byte.

    mod load hvi
    hvi file

## Why it is not called `vi`

A module's builtins become commands, so a module called `vi` makes `/usr/bin/vi`
unreachable. For the cat that shadowing is the point — it is byte-identical in
a pipe, so nothing can tell. This is not: there are no counts, no `.`, no
registers and no marks, and somebody reaching for `vi` out of habit would find
a different editor holding their file. **Shadow only when the replacement is
complete, or when being wrong is harmless.**

| file | role |
|---|---|
| `vi.h` | the buffer, the editor state, the edit record |
| `buf.c` | the gap buffer, the line index, loading and saving |
| `undo.c` | the edit log, and undo and redo over it |
| `vi.c` | modes, motions, operators, the ex line, drawing |

## The buffer is a gap buffer, not a piece table

The text is one allocation with a hole in it at the cursor, so inserting where
you are already typing copies nothing. A line index — a vector of line-start
offsets — is rebuilt **from the edit point forward**, never from zero, and only
when something asks for a line.

That is what the design is for, and it is measured rather than asserted. On a
50 MB file:

| | |
|---|---|
| load | 29 ms |
| the first line index | 306 ms |
| **200 edits at the far end, re-indexing after each** | **0 ms** |

A piece table was considered and rejected. It earns its complexity by making
undo a snapshot of the piece list — but undo here records edits rather than
snapshots, so that advantage does not arrive and only the cost does.

## Undo is linear, deliberately

Each entry holds what would have to be put back: the text removed for a delete,
the text added for an insert. Undo of an insert is a delete of that range; undo
of a delete is an insert of that text. Entries carry a group number, and
consecutive keystrokes in insert mode share one — which is what makes `u` after
typing a sentence remove the sentence rather than one letter.

**Linear with a redo stack, not a tree.** Vim's default is linear and the tree
is an extension almost nobody uses; retrofitting a tree later is a rewrite, so
the choice is written down here rather than left to be discovered.

## Writing

`:w` writes to a temporary file beside the target and renames it, so an
interrupted write cannot truncate the file you were editing.

`:w` **refuses if the file changed on disk** since it was read — compared on
modification time to the nanosecond and on size — and says so. `:w!` overwrites
anyway. That is vim's behaviour and it was the owner's call.

## What is there

Motions `h j k l 0 ^ $ w b e gg G`, arrows, `ctrl-f ctrl-b ctrl-d ctrl-u`.
Insert with `i a I A o O`. Operators `d c y` with any of those motions, plus
`dd cc yy`, `x X D C J p P`. Undo `u`, redo `ctrl-r`. Visual `v` and `V`, which
take in the character under the cursor as vi does. Search `/` with every match
on screen highlighted, `n` and `N`. Ex: `:w :w! :q :q! :wq :x :e :e!` and a
bare line number.

## What is not

No counts (`3dd`), no `.` to repeat, no registers beyond the one unnamed yank,
no marks, no `:s`, no `!` to filter through a command. That last one is the
notable gap: when it arrives it must call `hibr_run`, not `popen`, so it sees
the shell's own functions and variables — the editor is not allowed to become a
second shell.

## Colouring

There is syntax colouring, and it is the cat's — asked for through the module
registry rather than copied:

```c
hl = hibr_require(s, "highlight", HL_API_VER);
```

So the editor never mentions the cat, the shell finds whatever offers
`highlight` and loads it, and there is one set of language tables rather than
two that drift. The colourer returns a line with ANSI escapes in it; the editor
reads those back into display pens, walking the coloured copy alongside the
buffer so selection and search still paint over the top.

Block state runs from the top of the *file*, not the top of the screen, or a
comment that opened above the viewport would not colour what is inside it.

hibr has its own language in those tables now, rather than being treated as
`sh`: `fn`, `ret`, `fail`, `try`, `opt`, `args`, `match`, `rsub`, `str`, `arr`,
`json`, `mod`, `listen`, `coproc` and the rest, for `.hibr`, `.hibrc` and
`.t`.

## Testing

`tests/hvi.py` drives it through a pseudo terminal and mostly checks the file
that comes out, which is what an editor is for. 30 checks.
`tests/680-hvi.t` covers what `run.sh` can reach, which is the argument handling
and the refusal to start without a display. The gap buffer has its own C test, `tests/hvi-buf.c`, which is where the
measurements above come from:

    gcc -Iinclude -Imods/hvi -w -o /tmp/vibuf tests/hvi-buf.c mods/hvi/buf.c src/mem.c
    /tmp/vibuf
