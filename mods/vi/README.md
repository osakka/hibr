# mods/vi

Modal, and actually vi — but the arrow keys work, undo goes back more than
once, and UTF-8 is a character rather than a byte.

    mod load console
    mod load vi
    vi file

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

No syntax highlighting either. The cat has a lexical one; if it belongs here
too, `hl.c` can be offered through the module registry exactly as the display
is, rather than copied.

## Testing

`tests/vi.py` drives it through a pseudo terminal and mostly checks the file
that comes out, which is what an editor is for. 26 checks.
`tests/680-vi.t` covers what `run.sh` can reach, which is the argument handling
and the refusal to start without a display. The gap buffer has its own C test, `tests/vi-buf.c`, which is where the
measurements above come from:

    gcc -Iinclude -Imods/vi -w -o /tmp/vibuf tests/vi-buf.c mods/vi/buf.c src/mem.c
    /tmp/vibuf
