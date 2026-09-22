# Using it every day

- **`~/.hibrc`** (or `$HIBR_RC`) is read by interactive shells only.
- **Prompts** take `\u \h \H \w \W \s \v \j \$ \n \t \T \@ \d \e \a` and `\[ \]`;
  after the escapes, the prompt goes through normal expansion, so
  `$(git branch --show-current)` works. Colour does not move the cursor.
  `PS1` is the prompt and `PS2` the continuation.
- **A right-hand prompt** in `RPS1`, drawn against the right edge of the first
  row and given the same escapes and expansion as `PS1`. It appears only when
  the line leaves room for it, so a long command simply takes the space back,
  and it never moves the cursor.

      RPS1='[\t]'                       # the time, on the right
      RPS1='$(git branch --show-current)'

- **A transient prompt** in `TPS1`. When a line is accepted it is redrawn with
  this shorter prompt before the command runs, so scrollback keeps the commands
  and not the decoration. The right-hand prompt is dropped from that line too.

      PS1='\u@\h \w\$ '                 # what you type at
      TPS1='\$ '                        # what stays behind
- **Line editing** handles UTF-8, combining marks (one backspace removes a
  letter with its harakat), wide CJK characters, lines that wrap, and terminal
  resizing. The usual Emacs keys, arrows, Home, End and Delete.
- **History** is saved to `$HIBR_HISTFILE` or `~/.hibr_history`. `^R` searches
  it incrementally. `!!`, `!$`, `!n`, `!-n` and `!prefix` expand in
  interactive shells (`set -H`/`+H`); `history -s` adds an entry and
  `history -p` shows an expansion without running it.
- **Completion** offers builtins, functions and everything on `PATH` in command
  position, variable names after `$`, and paths elsewhere. It is programmable:
  set `COMPLETE[git]=git_complete` and that function's `ret` values become the
  candidates.
- **Aliases**, a **directory stack** (`pushd`, `popd`, `dirs`, `cd -`) and
  **`CDPATH`**.
- **`select`** menus, and `read` with `-r -p -n -t -s -u`.
- **Job control**: every interactive foreground job gets its own process group
  and the terminal. `^Z`, `fg`, `bg`, `jobs`, `wait` (by job or PID), `kill`
  with signal names, `disown`. Scripts skip all of it and pay nothing.
- **`title`** renames the running process for `ps` and `top`.
- **`command`**, **`builtin`**, **`type`**, **`time`**, **`umask`**, **`exec`**,
  **`getopts`**, `trap` on signals, `EXIT` and `ERR`.

---

[← documentation index](README.md) · [← project README](../README.md)
