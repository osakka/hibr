# Using it every day

- **`~/.hibrc`** (or `$HIBR_RC`) is read by interactive shells only.
- **Prompts** take `\u \h \H \w \W \s \v \j \$ \n \t \T \@ \d \e \a` and `\[ \]`;
  after the escapes, the prompt goes through normal expansion, so
  `$(git branch --show-current)` works. Colour does not move the cursor.
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
