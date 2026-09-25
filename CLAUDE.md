# CLAUDE.md

Context for working on **hibr** — a small, fast, bash-flavoured shell in C,
built with tcc. Read this first; it records the conventions, the design
decisions and the traps already found, so they don't get rediscovered.

## What hibr is

A shell that runs a useful subset of bash syntax in ~15,000 lines and a 358 KB
binary, with resident memory about half of bash's (and about 110 kB more than
dash's — see the README's own Measurements table for the honest comparison),
and faster than bash on tight loops. It is *not* a drop-in bash replacement
and must not be described as one: some divergences are deliberate (see Design
decisions).

Its reasons to exist beyond size: nested maps, JSON with type fidelity, regex
capture, in-process text/array operations, native TCP/TLS/Unix sockets and
servers, typed function signatures, result slots (`x := f` without forking),
declared CLI arguments, and a module ABI that lets modules add *protocols*
(`/dev/<name>/…`), not just commands.

Version and ABI: `HIBR_VER` and `HIBR_ABI` in `include/hibr.h` (0.21, ABI 14).

## Build and test

    make                 # tcc; builds ./build/hibr and mods/*.so
    make TLS=0           # compile TLS out entirely
    make CC=gcc OPT=-O2  # optimised: ~20% smaller text, ~45% faster, not the default
    make check           # = tests/run.sh
    make install         # PREFIX=/usr/local, modules to $(PREFIX)/lib/hibr
    ./build/hibr -n script      # parse only

    tests/run.sh [-v] [prefix]           # C-side harness, 82 tests
    ./build/hibr tests/self.hibr                 # suite in hibr, 91 assertions, planned
    python3 tests/{console,cat,most,hvi,mon,mtr,editor,desktop,apps}.py
                                 # the full-screen suites, each through a pty
    python3 tests/screen.py examples/desktop-session.hibr
                                 # the same harness, to look rather than assert
    python3 tests/diff.py --shell ./build/hibr 250   # snippets, diffed against bash
    python3 tests/corpus.py --list <file>        # real scripts, run under both shells
    HIBR=./build/hibr REF=dash tests/run.sh      # compare against another shell

Sanitizers — run both before calling anything done:

    gcc -Iinclude -DHIBR_TLS -g -O1 -fsanitize=address,undefined \
        -fno-sanitize-recover=undefined -w -rdynamic -o build/hibr.asan src/*.c -ldl
    ASAN_OPTIONS=detect_leaks=0 HIBR=./build/hibr.asan tests/run.sh
    ASAN_OPTIONS=detect_leaks=1 ./build/hibr.asan tests/<one>.t    # leak check
    SEED=7 python3 tests/fuzz.py ./build/hibr.asan 500             # parser fuzzing

On a kernel with high ASLR entropy the sanitizer build loops printing
`AddressSanitizer:DEADLYSIGNAL` instead of running a single command. Disable
randomisation for it — wrap the binary in `setarch -R` and point `HIBR` at the
wrapper script.

Dependencies: libc and libdl only. libssl is `dlopen`ed on first TLS use, never
linked, and no OpenSSL headers are needed to build.

## Coding conventions (non-negotiable)

- **C, built with tcc.** Everything must compile under `tcc` with no warnings
  that matter. gcc is used only for sanitizers and profiling.
- **No comments in code**, except a single-line description above every
  function. Prefer log messages (`lg(HIBR_LDBG, ...)`) over comments to explain
  behaviour; they are visible with `hibr -d 3`.
- **No static buffer sizes.** Everything is dynamically allocated: `str` for
  strings, `vec` for lists, arenas for AST and expansion temporaries. A
  fixed-size local array for data is a bug. (Constant chunk sizes used for
  dynamic allocation, like `HIBR_IOCH`, are fine.)
- **No nested/local functions.** Top-level functions only; they are non-static
  so modules can reach them with `-rdynamic`.
- **Short, memorable names** — `ex`, `xw`, `lx_next`, `v_setp`, `rd_do`.
- **Public macros are guarded** with `#ifndef` so they never need redefining.
- **Unified types**: `str`, `vec`, `arena`, `word`, `part`, `node`, `var`,
  `ent`, `sh` are shared across the whole codebase and the module ABI.
- Full runnable code, never placeholders.

## Source layout

| file | role |
|---|---|
| `include/hibr.h` | public types, macros and the module ABI (v14) |
| `include/pri.h` | internal declarations, tokens, the `lex` struct |
| `include/re.h` | our own regex declarations (tcc cannot parse glibc's) |
| `src/mem.c` | arenas with mark/release, `str`, `vec`, pools, `lg` logging |
| `src/var.c` | variable table; ordered nested maps (`ent`); arrays are maps |
| `src/lex.c` | tokens, quoting, `$…` parts, heredocs, `((…))`, `$'…'` |
| `src/parse.c` | recursive descent parser to an arena AST |
| `src/expand.c` | word expansion, splitting, globbing, braces, arithmetic |
| `src/exec.c` | execution, redirections, pipelines, functions, `[[ ]]`, loops |
| `src/bi.c` | the sorted builtin table and most builtins |
| `src/job.c` | job control, process groups, terminal handover |
| `src/trap.c` | traps, `fail`/`try`, `printf` |
| `src/edit.c` | line editor (UTF-8, wrapping), history, completion, `!!` |
| `src/daily.c` | aliases, dirstack, prompt, `.hibrc`, `command`, `time`… |
| `src/net.c` | sockets, `/dev/tcp|udp|tls|unix`, schemes, `listen` |
| `src/json.c` | JSON parse/emit/query over the map model |
| `src/text.c` | `str` and `arr` builtins |
| `src/regex.c` | `match` and `rsub` over POSIX ERE |
| `src/args.c` | `opt`/`args` declared CLI parsing, `title` |
| `src/mod.c` | module loading |
| `mods/*.c` | reference modules: `sys`, `http` (scheme), `ls` |
| `examples/desktop.hibr` | the window manager, in hibr — see `docs/desktop.md` |
| `examples/apps/` | apps for it: a calculator, a file browser, a control panel, a terminal, and three games (snake, mines, bricks) |
| `tests/screen.py` | **the** pty harness and terminal model, shared by every full-screen suite |
| `mods/prompt/` | the prompt module, including a native reader for git's object store — see `mods/README.md` for the file-by-file breakdown |
| `mods/console/` | the text display: alternate screen, cell grid with damage-based redraw, panes, decoded keys — see `mods/console/README.md` |
| `mods/display.h` | the interface a display backend offers; `console` is the only one so far |
| `mods/cat/` | `cat` that is byte-identical in a pipe and useful on a terminal — see `mods/cat/README.md` |
| `mods/trace/` | unprivileged traceroute over UDP with `IP_RECVERR`, one-shot and live — see `mods/trace/README.md` |
| `mods/most/` | a pager on the display interface, the first module to use another — see `mods/most/README.md` |
| `mods/hvi/` | hibr's vi: gap buffer, lazy line index, linear undo — see `mods/hvi/README.md` |
| `mods/mon/` | a system monitor over `/proc` — see `mods/mon/README.md` |
| `mods/sysinfo/` | what the machine is, with a picture — see `mods/sysinfo/README.md` |
| `mods/pty/` | pseudo terminals: run a program on one and drive it — see `mods/pty/README.md` |
| `mods/term/` | a terminal emulator: a program's screen as cells, drawn into a window — see `mods/term/README.md` |
| `mods/hold/` | sessions that outlive their terminal: detach, log off, attach again — see `mods/hold/README.md` |

Each directory carries its own `README.md` with the detail: `src/`, `include/`,
`mods/`, `tests/`, `examples/`. User-facing documentation is under `docs/`, and
every deliberate divergence from bash has a record in `docs/adr/`. Keep those
current — this table is a summary, not the source of truth.

Every guide page is now written to the same standard: each claim run before it
was written down, and the output pasted back. `docs/interactive.md` was the
last thin one and its key table, completion behaviour and prompt escapes were
each checked through `tests/editor.py`'s pty before being described.

`docs/grammar.md` states the language: lexical structure, an EBNF grammar,
precedence tables and expansion order, all taken from `lex.c` and `parse.c`
rather than from memory. `docs/builtins.md` covers every builtin.
`tests/530-docs.t` fails when a builtin is added without a reference entry,
when an internal link stops resolving, or when a decision record is not
indexed, and `tests/540-examples.t` runs every example in `examples/`, so
none of it can rot quietly. Examples in `docs/data.md` and `docs/cookbook.md`
were each run and their real output pasted back; keep it that way.

## Core model, in brief

- **Memory.** `sh.ar` holds the AST; `sh.xa` holds expansion results and is
  marked/released per command, so loops don't grow. Function bodies keep their
  arena by moving it to `sh.held`. Scratch `str`/`vec` come from pools.
- **Words.** A `word` is a list of `part`s (`P_TXT` with a quote flag, `P_VAR`,
  `P_CMD`, `P_ARI`, `P_PSUB`). Expansion builds the text plus a per-byte quote
  *mask*; splitting and globbing only act on unquoted bytes.
- **Variables.** `var` holds a scalar and optionally an ordered map of `ent`,
  each `ent` a scalar or another map. `ty` carries JSON types. `am` marks a
  (possibly empty) map. Subscripts: all digits = literal key; containing an
  operator = arithmetic; bare name = its value if numeric, else literal.
- **Result slot.** `ret` sets `$RET`; `x := cmd` clears the slot, sets
  `sh.bind` so builtins fill it silently, runs the command, binds `x`.
- **Builtin table is sorted** and searched with `bsearch`. After adding a
  builtin, keep it sorted — `tests/260-builtins.t` fails if a name stops
  resolving.

## What belongs in the shell, and what does not

hibr is a bash replacement first. That is the thing it must not lose, and it is
easy to lose by accident, one convenient addition at a time.

**The core grows only for things that make it a better shell** — bash
compatibility, expansion, job control, the module mechanism itself. Everything
else is a module, and every module is `dlopen`ed on demand: a shell that only
runs your scripts loads none of them and pays for none of them.

**Tools are modules. Applications are scripts. What the user configures is a
script of their own** — not a configuration file format. The desktop follows
X's shape: the display is a module you load and unload, the window manager is a
script you run and exit, and the session file is the user's. Nothing stays
resident after it returns.

Something user-facing wanting to be in the core is the signal that it should be
a module. Measure before believing an addition is needed there: the whole of
one working day's tools — a pager, an editor, a monitor, a cat, a traceroute, a
display layer — cost the binary 5.7% and the loop nothing, because none of it
went in the shell.

## Design decisions (deliberate divergences from bash)

1. **errexit is scoped.** Exemption applies only to the tested pipeline, not to
   bodies of functions it calls. Any failing pipeline stage fails the pipeline
   for errexit (no `pipefail` flag). Interactive shells abandon the line.
2. **`((expr))` never trips `set -e`** — its status is a value, not a failure.
3. **`=~` and `match` capture into `M`**, not `BASH_REMATCH`.
4. **Strict expansion is opt-in** with `set -S`: expansions never split or glob.
   Default stays bash-like until real scripts have been run under both.
5. **`**` globstar is always on**; symlinked directories are not followed.
6. **Brace expansion is literal-only**: `{$a,$b}` is not expanded.
7. **`ret` does not print**; functions return through `$RET` / `:=`.
8. **Arrays are sparse maps**, matching bash's counts and keys. A **quoted
   subscript is a literal key** (`h["content-type"]`); an unquoted one is still
   evaluated arithmetically.
9. **A command substitution does not inherit errexit**, matching bash's
   default; `shopt -s inherit_errexit` restores the POSIX behaviour dash has.
   **TLS verifies certificates by default**; `HIBR_TLS_INSECURE=1` to disable.
   **Options live in one namespace**: `set -o` and `shopt` reach the same
   table, extended patterns need no switch, and an option that cannot move
   says so rather than appearing to succeed — see `docs/adr/0017`.
   **Privilege only ever goes one way**: `drop` gives up root irreversibly,
   hibr is never setuid, and root loads modules only from `HIBR_MODDIR` — see
   `docs/adr/0016`.
10. **`args` exits the script only at top level**; inside functions or `try` it
    returns 2.

## Traps already found — don't reintroduce them

- **Flush stdio around every fork and redirection** (`fflush(0)`), or buffered
  output is duplicated or reordered across processes.
- **`&` must wrap only the last and-or list**, not the accumulated sequence.
- **`$$` is captured at startup**, not `getpid()` at expansion time.
- **`R_DUP` with a `{name}` variable** must act on the fd in the variable.
- **Redirection failure on the exec-in-place path** must `_exit`, not exec.
- **`xm` does not zero, so every record it returns must be.** `var` and `ent`
  crash outright on an uninitialised map pointer, which is how this was first
  found; a plain struct fails quietly instead. The pty module set five of its
  eight fields and inherited `done` and `st` from the freed record malloc
  handed back, so every child reported the *previous* child's exit status and
  looked plausible doing it. `memset(p, 0, sizeof *p)` after every `xm` of a
  record.
- **Bound recursion**: parser `HIBR_DEPTH`, arithmetic `HIBR_AXDEPTH`, `[[ ]]`
  grouping. Deep input must produce an error, never a stack overflow.
- **Arithmetic overflow** is done in unsigned and cast back; `INT64_MIN / -1`
  is special-cased. Keep the evaluator clean under UBSan.
- **`printf` widths** must size the buffer (`%02000d` overflowed once).
- **tcc cannot parse glibc `regex.h`** — use `include/re.h`.
- **Builtins parsing their own flags must stop at the first non-flag**, so a
  subject like `-rw-r--r--` isn't mistaken for an option.
- **The prompt hook must save and restore shell state.** `pr_hook` runs a
  command between two user commands, so `st`, `stop`, `intry`, `bind`, `xerr`,
  `brk`, `cont` and `ret` are all saved and put back; otherwise a `fail` in a
  prompt function silently swallows the next command. It sets `bind` only for
  builtins, never for functions, matching `ex_cmd`.
- **The hook's result is final text.** It never goes back through `pr_esc` or
  `xone`, or a `$` in a branch name would be expanded.
- **`ed_pos` counts prompt rows**, so anything that can put a newline or an
  escape sequence in a prompt has to be accounted for there.
- **A too-broad skip in `tests/diff.py` reports success.** Its `DELIBERATE`
  patterns must match only the construct they name; filtering on `{` for brace
  expansion also caught `${x}` and every function body, skipped three quarters
  of the runs, and made a silent generator look like a clean shell.
- **The line editor can only be tested through a pty**, which is what
  `tests/editor.py` is for; `tests/run.sh` cannot reach it, because the editor
  runs only when stdin is a terminal. Do not name that file `pty.py` or
  `tty.py` -- either shadows a module `pty` itself imports, and the failure
  reads as a circular import rather than as a name clash.
- **Object reads are bounded.** `grepo.omax` caps how large an object may
  inflate to (`PROMPT[git][max_object]`, 4 MB by default), so a crafted object
  cannot make a prompt allocate hundreds of megabytes. Pack entries are bounded
  again by the size their own header declares, and delta chains by
  `OB_MAXDELTA`. Real chains reach depth 50.
- **Object names are not verified.** Nothing recomputes the SHA-1 of what it
  reads, the same as git on a normal read; a damaged object is caught by
  inflate failing or by a header that does not parse.
- **Mode changes are compared separately from content.** A `chmod -x` leaves
  the blob identical, so checking the hash alone reports the file as clean.
- **The racy-index rule is git's**: an entry is suspect only when the index is
  older than the file, or the same second with `index nsec <= file nsec`. A
  looser rule rehashes the whole tree on every prompt.
- **Every expanded field leaves `expand.c` through `xout`/`xoutq`**, so the
  field vector and its parallel quote-mask vector cannot drift apart. A desync
  would hand a builtin the mask of a different argument and make `unset` delete
  the wrong key. `xargv` compares the two lengths and drops the mask rather
  than trust it; glob results are padded with unquoted entries by `xpad`.
- **One helper splits `name[sub]…`**, `bi_keys`, and everything that takes a
  subscripted name goes through it: `unset`, `read`, `[[ -v ]]` and the
  assignment path. It honours a quoted subscript and a negative one, and it
  leaves the name truncated at the bracket for the caller to restore. A builtin
  that wants the quoting of its own arguments must also be named in `bi_mask`,
  or it silently gets none -- which is what made `read h["a-b"]` write to key 0
  after the parsing was already right.
- **`al_quote` reproduces the caller's quoting rather than inventing its own.**
  An alias re-emits arguments that have already been expanded and are then
  re-lexed, so what it writes decides what the new mask says. Given the old
  argument's mask it wraps the runs that were quoted in `'…'` and escapes the
  rest per character, which reproduces the same mask — so `u h["a-b"]` still
  reaches the literal key and `u a[i]` still resolves `i`. Wrapping everything
  in quotes breaks the second; escaping everything breaks the first. With no
  mask it falls back to escaping, and an argument that is empty or holds a
  newline is quoted whole, since `\<newline>` is a line continuation.
  `xargv` builds the mask for a command whose first word names an alias, as
  well as one in `bi_mask`.
- **Argument masks are gated on the command name.** `xargv` builds them only
  when the first expanded word names a builtin in `bi_mask`, so an ordinary
  command pays one `strcmp` and nothing else — building them unconditionally
  cost 7% on tight loops. A builtin that grows an interest in its arguments'
  quoting has to be added to that list, and `command` must keep forwarding
  `sh.amask + 1` with its shifted `argv`.
- **An unquoted subscript is looked up as a variable first.** The rule is
  "bare name = its value if numeric, else literal", so `${TRACE[0][hops]}`
  reads the *variable* `hops` when one exists and is numeric: a script with its
  own `hops=20` silently addresses key 20 and gets nothing, and `DT[$id][row]`
  in the window manager addressed key 6 because `local row=$3` is the obvious
  name for a mouse row. Quote every literal subscript —
  `${TRACE[0]["hops"]}`, `${DT[$id]["row"]}` — which is what ADR 0006 says and
  what this has now cost an hour and then an afternoon to rediscover.
- **`rsub` replaces one match unless given `-g`.** A hostname cleaned with an
  ungreedy `rsub` keeps most of its dots, and the leftover text then reaches an
  arithmetic subscript and errors there, several steps from the cause.
- **Leading zeros are octal in arithmetic.** tzdata writes longitudes as `074`,
  which evaluates to 60 rather than 74 and puts a place fourteen degrees out.
  Strip them before the arithmetic sees them; bash does the same thing.
- **A map drawn by eye is wrong.** The world map in `examples/traceroute.hibr`
  scored 46% when hand-drawn — over half of real places fell in the sea. It is
  now written as longitude ranges per latitude band, which can be checked
  against an atlas, and scored against every coordinate in `zone1970.tab`. Do
  not adjust it by eye; re-score it.
- **A tool that writes to standard output must be plain in a pipe.** The cat
  module decides everything on `isatty(1)`: in a pipe it is byte-identical to
  `/bin/cat`, including `-n` and `-A`, which must produce GNU's bytes and not a
  nicer version of them. `tests/650-cat.t` compares against `/bin/cat` itself
  rather than against bash, because bash's `cat` *is* `/bin/cat`. The fast path
  (`ct_raw`) never looks at a byte, which is why 100 MB costs 15 ms against
  `/bin/cat`'s 14; anything that inspects content has to stay off it.
- **Mouse reporting is off until asked for, and the enable is easy to forget.**
  The console shipped with the mouse *decoder* written and the *disable* in its
  leave sequence, but nothing ever sent the enable -- so clicks produced
  whatever the terminal does by default and the decoder never saw a report.
  `console mouse click|drag|motion` turns it on; off stays the default because
  reporting takes click-and-drag text selection away from whoever is watching.
- **Never write a string's length by hand.** `sizeof x - 1` on a named
  constant, or `strlen`. Counting escape sequences by eye has now cost two
  bugs: a truncated `\e[2J` in the console's enter sequence, which left the
  alternate screen unclear; and a four byte read past the end of the mouse
  disable string, which ASan caught only because the mouse decoder was being
  fed malformed reports at the time.
- **A descriptor lives inside the object, so read it before dlclose.**
  `mod drop all` logged `m->m->nm` after unmapping the module and segfaulted.
  Take a copy of anything needed from the descriptor first.
- **A script says what it needs; a module says what it offers.** `need <name>`
  is `hibr_require` with a status instead of a pointer, so a script can reach
  the autoloader too. It does not check the version -- present is enough, and
  asking `m_offered` for version 0 makes it complain about a mismatch nobody
  asked about. One registry resolves both directions.
- **A module declares the interface it offers, so it can be found unloaded.**
  `hibr_require` walks the module path when nothing has offered what was asked
  for, reads each descriptor's `prov` without calling its init, and loads the
  first match. That is what lets a tool ask for `"display"` and never mention
  `console`. A module that registers an interface in its init must also name it
  in `HIBR_MODULE_P`, or nothing will find it before it is loaded.
- **tcc accepts a newline inside a string literal; gcc does not.** A generated
  edit put one there and `make` was clean, because tcc is the default. Anything
  editing source mechanically wants a `gcc -fsyntax-only` pass as well, which
  is cheap and catches what tcc waves through.
- **A sanitizer run can hang and spin.** One was found at 100% of a core after
  21 hours -- a `tests/120-bg-order.t` under ASan from the previous day. It
  does not reproduce, so it is intermittent rather than fixed. After sanitizer
  work: `ps -eo pid,etime,pcpu,comm | grep hibr`.
- **A live display cannot be sampled mid-frame.** `tests/mon.py` reads the
  process table from a *paused* monitor, because a running one redraws during
  the capture and only changed cells are sent -- so the reconstruction can hold
  one row from before a re-sort and the next from after it. That reads as an
  ordering bug and is not one. It passed alone and failed once after the other
  suites, which is how a flake announces itself.
- **`/proc/[pid]/stat` field three is a letter.** A loop that skips fields by
  reading numbers never passes the process state, and every field after it
  reads zero -- which looks like an idle machine rather than a parsing bug. The
  command name is in brackets and may contain brackets, so it is found from the
  *last* `)` in the line.
- **A string index matches a prefix.** `index("### A vi")` found
  "### A visual traceroute" and an edit anchored on it silently removed three
  sections of `docs/backlog.md`. Anchor on a whole line, and check the section
  count before and after.
- **Modules reach each other through the registry, not the linker.** `m_open`
  uses `RTLD_LOCAL` on purpose, so a symbol in one module is invisible to the
  rest. `hibr_provide(s, nm, ver, table)` in a provider's init and
  `hibr_require(s, nm, ver)` in a user's is the way across; the version must
  match exactly, and the provider must `hibr_unprovide` in its finaliser or a
  dropped module leaves its users holding a table in an unloaded object.
  `most` uses the console this way, and asks for `"display"` rather than for a
  backend. Do not switch to `RTLD_GLOBAL` to avoid it —
  that makes every module's symbols collide with every other's, which is the
  `m_drop` trap generalised.
- **A round of probes must be spaced, and collected between the spaces.**
  Sending every hop limit at once is what makes a live traceroute fast enough
  to watch, but routers rate limit their ICMP and the queueing lands in the
  timings -- the same hop reads 35 ms probed singly and 520 ms in a burst. So
  the sends are spaced, *and* replies are picked up between them: timing a
  reply when the round ends rather than when it arrives charges the wait for
  later hops to the earlier ones, and a LAN gateway reads 200 ms. Either half
  alone is wrong, and each looks plausible on its own.
- **Visual mode takes in the character under the cursor.** vi's `v` is
  inclusive and a naive start-to-cursor range is one character short, which
  looks right on a long selection and wrong on a short one. `V` is whole lines
  and does not need it.
- **A damage-based renderer cannot be tested by grepping its output.** The
  display sends only the cells that changed, so the byte stream holds
  `78-200/200` where the display reads `178-200/200`. `tests/most.py`
  reassembles the screen from the escapes and asserts on that; four checks
  looked like real failures until it did.
- **Shadow a command only when the replacement is complete, or being wrong is
  harmless.** The cat shadows `cat` on purpose: it is byte-identical in a pipe,
  so nothing can tell. The editor does not shadow `vi`, because it has no
  counts, no `.`, no registers and no marks, and somebody reaching for `vi` out
  of habit would find a different editor holding their file. It is `hvi`.
- **A module's builtins become commands, so its name shadows one.** The
  display module was called `screen` and `mod load screen` made
  `/usr/bin/screen` unreachable. For the cat that shadowing is the point; here
  it was not. Check a new module's name against `command -v` before choosing
  it. The module is `console` because it is a text display, and what it
  registers is `"display"` — the interface, not the backend — so a framebuffer
  or SDL backend could replace it without `most` or the vi changing.
- **The `sc_` prefix is `net.c`'s.** The display module is `cn_` now and was
  `scr_` before that, because
  `src/net.c` already exports `sc_find` and `sc_fini` for schemes — and
  `sc_fini(sh *)` is exactly the signature a module finaliser has, so a screen
  module calling its own `sc_fini` would have been silently bound to the
  shell's scheme cleanup. Nothing would crash; the terminal would simply never
  be put back. This is the `m_drop` trap below, found a second time, which is
  why the check belongs in the build habit and not in memory.
- **A full-screen redraw must cost what changed, not what is on screen.** The
  console module keeps a front and a back grid and emits the difference: 2095
  bytes for the first paint, 8 for one changed character, and **zero** for a
  flush with nothing new. Two things that look like optimisations are not —
  re-sending the pen every flush, and moving the cursor over a one-cell gap.
  A cursor move is three bytes and the character it skips is one, so `cn_near`
  redraws gaps of up to two cells rather than jumping them.
- **The cursor advances on its own after a character is drawn.** Track the
  column *after* the glyph, not the one it started at, or every cell gets a
  spurious `\e[C` and the text spreads out one column at a time.
- **`cn_key` must not consume the resize flag.** `select` returns `EINTR` on
  `SIGWINCH`; treating that as an error, or swallowing the flag to report it,
  leaves the application unable to learn that the terminal changed size. The
  wait ends early and `screen resized` still answers.
- **A module must not name a function the shell already exports.** The shell is
  linked `-rdynamic`, so a module's own global symbol is preempted by the
  shell's of the same name: `sys.c` defining `m_drop` silently bound to
  `mod.c`'s `m_drop(sh *, const char *)` and crashed on the first call with a
  signature mismatch. The `m_` prefix is `mod.c`'s, so modules use their own —
  `sy_`, `pr_`. To check one:
  `nm -D build/mods/x.so | awk '$2=="T"{print $3}' | sort -u |
  comm -12 - <(nm -D build/hibr | awk '$2=="T"{print $3}' | sort -u)`
- **`declare` speaks the shell's own type names.** `declare -i` is bash's, and
  coerces: garbage becomes 0. `declare int x` is ours, and validates with the
  same `ty_ok` that checks typed function parameters, so a bad value fails
  loudly and stops. Do not make the typed form evaluate arithmetic first --
  that was tried, and `n=abc` quietly became 0 again, which is the behaviour
  the typed form exists to avoid.
- **An indirect expansion carries its modifier in a flag bit.** `${!ref:-d}`
  needs both the indirection and the `:-`, and `part` is public, so `V_INDF`
  (0x100) is ORed into `p->op` rather than a field being added to the struct.
  Anything comparing `p->op` for equality must therefore not expect to see a
  flagged op, and `xvar2` is given a copy with the bit stripped.
- **A forked child forgets the inherited exit trap.** `tr_fork` clears slot 0
  the moment a subshell, command substitution, background job or pipeline
  element starts, and `tr_exit` runs before each `_exit`. Without the clear the
  inherited trap fires once per child as well as in the parent; without the
  `tr_exit` a trap set inside the subshell never fires at all. Both halves are
  needed, and `tests/470-bash-gaps.t` checks each of the five paths in both
  directions. The exec-in-place path deliberately does neither, as exec
  replaces the process.
- **An assignment prefix must override, not accompany.** `v_envp` used to append
  `TZ=UTC` after the exported `TZ`, and the child took the first of the two, so
  `VAR=x cmd` silently did nothing when `VAR` was already exported. Exported
  variables also reach the process with `setenv`, or an in-process builtin --
  and any module -- cannot see `TZ` or `LC_*` at all.
- **`noclobber` only guards regular files.** bash lets `>` truncate a device,
  so `2>/dev/null` has to keep working under `set -o noclobber`; guarding
  everything made a recorded test capture the wrong behaviour for a while.
- **Do not replace a libc string or number function with C.** glibc's `strtol`,
  `strchr` and `strcmp` are hand-written assembly; the same logic compiled by
  tcc loses to them. A careful `strtol` replacement, verified against libc on
  116 inputs, made the benchmark loop **4.5% slower** and was thrown away. Cut
  the number of calls instead of trying to beat the call.
- **The expansion arena keeps a few released blocks.** `ar_rel` used to `free`
  every block past the mark, so once the base block filled, every command
  malloced a block and freed it again -- three of each per loop iteration.
  `ar_drop` now keeps up to `HIBR_ARKEEP` blocks of the standard size for the
  next command and frees anything larger, which costs about 36 kB of resident
  memory and bought 5%. `ar_free` and `ar_reset` must both drain that list.
- **Three fast paths carry the loop, and all must stay honest.** `xargv` skips
  brace expansion, `xwm`, splitting and globbing for a word that is one
  unquoted run of text with no byte in `w_meta`. `xwm` skips building the
  string and mask pair twice over: for a bare `$name` with no operator,
  subscript or quote -- whose value must have no `w_meta` byte, unless the
  caller wants a single word, where nothing splits or globs anyway -- and for
  a `HIBR_XPAT` word that is one unquoted run of text, since the escaping pass
  only ever escapes quoted bytes and there are none. All three bail out to the
  slow path on anything else, all are off under `set -u`, and together they are
  most of a 30% gain on a `while` loop and 10% on `case`. Anything added to
  expansion has to be reachable from the slow path, or a fast path has to learn
  to refuse it.
- **`IFS` is cached on `sh` and invalidated by hand.** Four of every six
  variable lookups in a tight loop were asking for it -- from `xargv`, from
  both expansion fast paths and from `xsplit`. `sh_ifs` answers from `ifsc`
  and anything that sets or removes a variable named `IFS` must clear
  `ifsok`, including `v_del`, or splitting silently uses a stale separator.
- **A command with no command word takes its substitution's status.** `V=$(cmd)`
  reports `cmd`'s status, not 0, which is what makes `V=`getopt -T`; test $? -eq
  4` work -- the idiom fakeroot and half of `/usr/bin` use to detect GNU getopt.
  `xcap` counts substitutions in `sh.ncap` and `ex_cmd` compares it across the
  whole expansion, because "the last substitution performed" has to include the
  ones in a word that expanded to nothing. A command word means the command's
  status instead, so `local x=$(cmd)` still masks it exactly as in bash.
- **An explicit `exit` is not an errexit failure.** `ex_chk` checks `s->quit`
  along with `s->stop`, or a `$(exit 1)` child under `set -e` prints a
  diagnostic for a status its own script asked for.
- **`getopts` keeps a position inside the argument**, in `sh.optpos`, reset when
  `OPTIND` is assigned from outside -- the only signal a script gives that it is
  restarting. Without it `-abc` is one option and `-c5` takes the next word.
- **`read` assigns at end of file and still fails.** Both halves: the variable
  becomes empty *and* the status is 1, including for a last line with no
  newline. Return 0 there and `while read` never terminates; skip the
  assignment and a loop that clears a variable by reading into it spins.
- **`test` special-cases one to four arguments before the grammar**, because
  that is what makes `[ x = -a ]` a comparison and `[ -n -a ]` a unary test
  rather than parse errors. Do not "simplify" it into the general grammar. And
  do not ask `t_isbin` before calling `t_two` -- `t_two` already declines a
  word that is not an operator, and asking twice cost 3.6% on the loop.
- **`qsort` is not given a NULL base.** An empty directory leaves `vec.p` NULL,
  and glibc declares the argument non-null, which UBSan reports.
- **Declaration builtins take assignments, not words.** `local`, `declare`,
  `typeset`, `export` and `readonly` are ordinary builtins, so their arguments
  went through splitting and globbing like anyone else's, and `local t=$2` with
  a space in `$2` quietly produced an empty `t`. bash treats a `name=`, `name[i]=`
  or `name+=` word on those commands as an assignment: expanded, never split,
  never globbed. `w_assign` tests the *unexpanded* word's leading literal, so
  `local $x` still splits — only a literal `name=` prefix counts.
- **The command-name check in `xargv` is one call, not several.** `bi_argk`
  returns a bitmask for both questions xargv asks about a command name — does
  it read its arguments' quote mask, does it take assignments — and switches on
  the first character before any `strcmp`. Two separate functions with five
  `strcmp`s each cost 1.1% on a loop that never calls either. Anything else
  that wants to know something about the command name belongs in that bitmask,
  not in a new call.
- **`sh -c cmd name args...` names `$0` with the first operand**, not with the
  shell. hibr used to make it `$1`, so every argument was off by one and `$#`
  one too many. bash and dash agree with each other here; hibr was alone.
- **Arithmetic never sees the quotes**, so quoting a subscript does not save
  you there. `$(( ))` and `(( ))` have their argument expanded with quote
  removal before the evaluator runs, so `$(( DT[$id]["col"] ))` looks for the
  key `col` *evaluated*, exactly as the unquoted form would. Read the field
  into a local first. `let` is the exception, because its argument is a string
  the shell never unquotes, and `ax_unq` strips the quotes for it.
- **A variable's value is itself an expression.** `x="3 * (4 + 5)"; echo $((x))`
  is 27 in bash and was 3 here, because `ax_get` took `strtol` of the first
  token. `ax_val` re-reads anything that is not a clean integer as an
  expression, bounded by `HIBR_AXDEPTH` so `a=a` errors rather than
  overflowing the stack. This is the mechanism every shell calculator is built
  on, so it was not optional.
- **A plain name must not reach the vec pool.** `ax_get` began calling
  `vb_get`/`vb_put` around the subscript split for *every* arithmetic variable
  read, subscripted or not, and that alone cost 2% on an arithmetic loop. The
  `strchr(nm, '[')` is the gate, and it belongs in the caller, before the
  pool is touched.
- **Arithmetic reaches subscripts through `bi_keys`, like everything else.**
  `ax_name` swallows the whole `name[i][j]` chain into the name, and `ax_get`
  and `ax_set` split it with the same helper `unset` and `read` use. That is
  what made `$(( a[i] + 1 ))` reach a nested map and `(( a[0] = 42 ))` work at
  all — bash has both and hibr had neither. Do not add a second splitter.
- **`:=` binds for a builtin, not for a function.** A helper that ends in
  `str repeat "$2" "$1"` and is called as `x := helper ...` prints to the
  screen instead of filling the slot, because the bind was set for `helper`,
  which is a function. Route the value through a variable and `ret`. In a
  full-screen program the symptom is a blank line where the drawing should
  be, not an error.
- **`pkill -f` matches the command running it.** The testing rules say this
  about the harness; it is just as true of a shell command. `pkill -9 -f
  "desktop.py"` inside a command whose own line contains `desktop.py` kills
  itself, and the result is an empty output and a status of 1 that looks like
  the test failing. Kill by pid, or match something the killer does not
  contain.
- **Run the sanitizer suite under `timeout`, and check for orphans after.**
  A run has now hung twice, on different tests (`120-bg-order`, then
  `140-case`), and an orphaned `tests/run.sh` reparented to init keeps
  spawning `hibr.asan` for hours, competing for ptys and making unrelated pty
  suites hang. It has not reproduced on demand either time. `ps -eo pid,ppid,cmd
  | grep run.sh` before believing a pty suite that stalls.
- **Before trying anything by hand that uses `need`, reinstall.**
  `./deploy.sh update --yes --quiet --no-test`. The tests `mod load` the
  module out of `build/mods`, but `need` autoloads from the module path, which
  is the *installed* copy — so a green test suite and a broken run by hand
  mean the installed module is yesterday's.
- **There is one pty harness, `tests/screen.py`.** There used to be six
  `run()`s and five terminal models across the full-screen suites, differing
  only in timings, so a fix to one fixed one. Anything a new suite needs goes
  into `screen.py`; do not start a seventh copy, and do not write a throwaway
  renderer either — `python3 tests/screen.py <args>` runs hibr and prints the
  screen. Its `tree()` is deliberately not called `path`, which would be
  shadowed by the parameter of that name in half the suites and fail as
  "'str' object is not callable" a long way from the cause.
- **A mouse report has four fields or five.** `press`, `release` and `drag`
  carry a button — `mouse press left 6 20` — and the wheel does not:
  `mouse wheelup 6 12`. Reading the position from a fixed argument works for
  one shape and silently reads the wrong field for the other, which is why the
  wheel did nothing at all. The action may also carry a modifier
  (`ctrl-press`), stripped with `${act##*-}`. One place parses it.
- **An app is told a click in the coordinates it draws in.** Not in some
  separate body coordinate system offset by one: an app that draws its keypad
  at pane row 4 is told a click at pane row 4. Two systems is one too many,
  and both of the first two apps got the conversion wrong in opposite
  directions before this was fixed.
- **`local a=$1 b=${M[$a]...}` cannot see `a`.** `local`'s own arguments are
  all expanded before it runs, so the name being declared is not set yet --
  which bash does too, and which has now cost three separate afternoons in
  this desktop because a map lookup keyed on the id makes it silent rather
  than empty-looking. Declare the id, then read the map on the next line.
- **An app's state belongs under the window id, never in a global.** Two
  windows of the same app share every global it has: two terminals become one
  shell mirroring itself keystroke for keystroke, two browsers cannot be in
  different directories, two calculators hold one sum. The terminal's version
  was worse than it looked -- both windows resized the one grid to their own
  size on every frame, so the garbled characters were the same bug wearing a
  different hat. Every callback is handed the id; everything remembered goes
  under it.
- **An app keeps what it needs, rather than reading the window manager's
  table.** The browser stashes the visible row count in `_draw`, which is the
  only thing told it, instead of computing it from `DT[$id]["h"]` — which also
  walked straight into the `local id=$1 vis=$((DT[$id]...))` trap, since `id`
  is not set while `local`'s own arguments are being expanded.
- **A list of shell tokens belongs in an array, not a string.** `CALC_KEYS`
  held the keypad as a string and `for k in $CALC_KEYS` globbed the `*` and
  the parentheses, so the calculator drew a directory listing where its
  operators should have been.
- **`:=` binds into a subscripted target too.** `m[$i]["k"] := f` works, and
  it has to: nested maps and result slots are both hibr's own, and a script
  that could not put a function's result in a map without a temporary
  variable was two features that did not compose. A plain name still binds
  straight from `n->s` with no expansion, because that is every `:=` in a
  tight loop; a subscripted one is kept as a word in `n->bw` and goes through
  `xone_q` and `bi_keys`, so a quoted key stays literal. That field is why
  the ABI went to 14.
- **An app that manages other windows calls the window manager, not its
  table.** `dt_ids`, `dt_title`, `dt_hidden`, `dt_raise`, `dt_new`, `dt_min`,
  `dt_del`. Reading `DT` depends on how the window manager happens to be
  written today, and the first app that tried it landed in the
  `local id=$1 vis=$((DT[$id]...))` trap for its trouble.
- **A window list is in creation order, not stacking order.** Stacking order
  reshuffles the list every time something is raised, and raising things is
  what a window list is for — so the row you meant to click moves out from
  under you. `"${!DT[@]}"` gives creation order because the maps are ordered.
- **The menu bar belongs to the window manager, not to a window.** Menus
  follow focus, which is System 7's model and the reason it maps onto a
  window manager at all. An app declares them with `<app>_menus` calling
  `dt_menu`/`dt_item`/`dt_sep`, the same prefix contract as `_draw`, so an
  app without menus shows the desktop's.
- **Nothing is reserved while the menu bar is shut.** F10 or escape opens it,
  and only then do letters mean anything. Direct modifier shortcuts were
  considered and rejected: ctrl collides with everything a terminal window
  will need, and alt with what a program inside one might. A desktop that
  eats ctrl-c is a desktop nothing can run in.
- **`dt_sub` hangs off the menu being declared, not the last one created.**
  After one `dt_end` the most recently created menu is the submenu that just
  closed, so a second `dt_sub` attached itself to the first submenu — and it
  looks almost right until you notice an item has gone missing from the
  parent. `MB_CUR` is the answer, and everything that adds a row uses it.
- **A window menu is always last, and always there.** An app with its own
  menus must not be able to hide the only way to move or close its window.
  With nothing focused its items are dimmed rather than removed: a menu that
  changes shape between one moment and the next is one nobody can learn.
- **Two menu items cannot share a letter.** Calculator and Clock both start
  with a C, so `dt_akey` walks the label for the first letter nothing has
  taken. An item that cannot be reached is worse than one with no shortcut.
- **An app is a prefix, not a command.** The window manager calls
  `<app>_draw`, `<app>_key`, `<app>_click`, `<app>_open` and `<app>_close`,
  asking `command -v` once which exist. One function answering a verb was
  tried first and every app swallowed every key, because a `case` that matches
  nothing succeeds and a successful `key` means "handled" — so `q` stopped
  quitting whenever a clock had focus. A prefix makes that impossible rather
  than merely documented.
- **Lengths and slices count characters.** `${#s}`, `${s:i:n}`, `str len`,
  `str slice` and `str index` all count characters; `str pad` and `str width`
  count *display columns*, because padding exists to line columns up and `漢`
  is one character in two of them. See `docs/adr/0021`. The byte-based version
  drew a box border nine characters long with half a character on the end.
  Anything new that measures text has to pick one of the two on purpose.
- **A child needs a session and a controlling terminal, not just the fds.**
  Without `setsid` and `TIOCSCTTY` a shell started on a pty has no terminal,
  runs no line editor and calls itself non-interactive — which reads as the
  program being broken rather than the pty being half-made. And a master
  whose child has gone reads `EIO`, not zero; that is the one place a pty is
  not a pipe.
- **The shell must not reap children it did not start.** `waitpid(-1, …)`
  collects anything, and `jc_poll` then threw away the status of any pid it
  did not recognise — so every program the pty module ran reported exit 0,
  plausibly. Job control now waits on each job's own process group, and
  `jc_anyone` does the same for `wait` and `wait -n`. Whatever forked a child
  is the thing entitled to its status.
- **`console size` before `console open` reads the real terminal.** It used
  to answer a fabricated 24 rows beside a real `ed_cols()` width, so half the
  answer was true — which is worse than either, and hid a pty resize working
  correctly for most of an hour.
- **`unset 'm[$k]'` is bash's idiom and the wrong one here.** In hibr a quoted
  subscript is a literal key, so that removes a key named `$k`. Write
  `unset m["$k"]`: the variable is expanded and the key is still literal.
- **A function defined twice in a script silently replaces the first.** The
  desktop's drop handler was called `dt_drop`, which is also the function
  that draws the open menu, so every frame "drew the menu" by dropping
  whatever was being dragged. Nothing errors; it simply stops working.
  `tests/540-examples.t` now fails on any name defined twice in an example
  or an app.
- **An ignored signal survives `exec`.** The hold server ignores `SIGHUP`
  so a logout cannot end it, and every program it started inherited the
  ignore: a held shell behaved as if run under `nohup`, and `sleep`s outlived
  `hold kill`. The pty module's child now puts `SIGHUP`, `SIGTTIN` and
  `SIGTTOU` back to the default along with the rest.
- **A test of anything that saves must have somewhere of its own to save.**
  The desktop writes its settings on every change, so the panel tests were
  one run away from changing the owner's theme, and a test that changed it
  made the next test start from it. `tests/screen.py` gives each run its own
  `XDG_CONFIG_HOME` and `XDG_STATE_HOME`.
- **Do not write a wrapper through a path that may be a symlink.** A
  sanitizer wrapper written to a scratch `build/hibr` that was a symlink to
  the real one replaced the shell binary with a 65-byte script, and every
  suite run after that would have tested the sanitizer build. `rm` the path
  first, or give the wrapper a name of its own.
- **A terminal test must not end with `q`.** `tests/apps.py`'s `run()`
  finishes by sending `q`, which a focused terminal passes to its program
  -- and any key snaps a scrolled-back view to the live screen, so a
  scrollback test saw nothing scrolled. Pass `end=None`.
- **A printable key arrives as itself.** `console key` reports space as
  `" "`, not `space`, and a letter as the letter; only keys with no glyph
  have names. Both games shipped matching `space` and did nothing on it.
- **`str pad` has no `-l`.** It pads on the right only, and given `-l` pads
  the literal text `-l`. To right-align in a window, put the text at
  `w - ${#t}`: the face is cleared before every draw, so nothing needs
  padding over.
- **A game steps on the clock, never on frames.** Frames also arrive with
  every key, so a snake moved once per frame sprints while an arrow is
  held, and one moved only on idle frames freezes while it is. Step while
  `$EPOCHREALTIME` says a period has passed, and ask for the next frame with
  `dt_want`; its request lasts one frame, so a paused or hidden game costs
  nothing.
- **`ob_hex` does not check what follows the digits**, because in `packed-refs`
  an object name is followed by a space. Callers check the length.
- **`ret` ends the function on the spot, the same as `return`.** They share
  one flag, so `ret ""; return 1` never reaches the `return` -- `ret` has
  already stopped the function, with status 0, which is what `x := f`
  then sees no matter what follows it. `dt_mhit` signalled a miss this way
  and a click on empty menu-bar space opened whatever the bar had last
  looked up, because `x := dt_mhit ... || dt_mclose` never took the `||`
  branch. A miss needs a bare `return 1` and nothing else -- `:=` has
  already cleared the slot before the call, so there is no `ret ""` to
  write. `return 0` right after a successful `ret` is the same dead code,
  just harmless, since `ret` already leaves status 0.
- **A CSI parameter's own sub-parameter is separated by `:`, not `;`.** A
  modern program sends `4:0` to turn underline off and `4:1` upward to pick
  a style, in place of the plain `4`/`24` pair an older one uses -- and
  `strtol` on `"4:0"` reads `4` and stops at the colon, silently turning
  underline on instead of off. Once that happened, everything typed after
  stayed underlined until a full `SGR 0` reset arrived, which is what
  running a program built with a modern terminal-styling library inside a
  hibr terminal window looked like: everything underlined. `tm_sub` reads
  what follows the colon; nothing else sent here uses one.
- **`ioctl(fd, TIOCGWINSZ, …)` can answer 0x0 for a real terminal's first
  moment.** Seen on some terminals right after a new window opens, before
  it has settled on a size -- asked that early, the console read "no
  terminal at all" and fell back to a fixed 80x24 that only an actual
  resize ever corrected afterwards. `cn_size` retries a handful of times,
  milliseconds apart, before giving up; a terminal that already knows its
  size answers on the first try and never sees the wait.
- **`?=` in a Makefile cannot override a built-in variable's default.**
  `CC`, `CFLAGS` and the rest already have a value from make's own implicit
  rules before the makefile is even read, so `CC ?= tcc` sees `CC` as
  already set (to make's own `cc`) and never fires -- silently building
  with `cc` everywhere, which happened to look like nothing was wrong until
  a platform where `cc` and `tcc` disagree. `$(origin CC)` tells the
  difference: `default` means nobody set it, and only then should a
  platform-specific fallback replace it, so an explicit `make CC=...` or
  `CC=...` in the environment still wins.
- **A pty test's last screen is whatever was drawn before the quitting key
  was read, not after.** `dt_run` never redraws once `DT_QUIT` is set, so
  the frame a test's `t.screen()` reconstructs is one iteration behind the
  key that ended it -- which is why an open menu, or a note that a later
  key would clear, still showed up in a final screenshot: nothing ever drew
  the frame where it was gone. Adding Quit's confirm box broke this the
  other way -- an extra key (`y`) meant an extra frame, and the box itself
  became the thing frozen in the last screenshot instead. Draw once more
  only from inside the path that clears the box, right before the confirmed
  command runs, not unconditionally after the loop -- an unconditional
  redraw there also closes a menu that a different, one-key quit (Quit on
  the hibr menu, still `dt_quit` directly, deliberately not this box) had
  left open for exactly that same one-iteration-behind reason.
- **`:=` does nothing for a program on the PATH.** It binds a *builtin's*
  own result, silently -- `uname`, `hostname`, `top`, `sysctl`, `ps`,
  `grep`, none of those are builtins, and `x := uname` leaves `x` empty
  while `uname` prints straight to the real terminal, underneath the
  console module's own cell-based drawing, wherever the cursor happens to
  be. That is what "the About window is writing outside its own box"
  turned out to be. Worse, the captured variable being silently always
  empty meant a platform check (`[ "$AB_OS" = Linux ]`) never actually
  matched on a real Linux machine either, so About and Task Manager ran
  their macOS branch there throughout -- which is why the meters never
  updated, since `top`/`vm_stat` do not exist to fork. `$(...)` is the real
  capture for a program; `:=` is for the builtin calls already elsewhere in
  the same functions (`dt_ms`, `str`, `console`), which is where it stays.
  Fixed input standing in for a real command's output is not the same as
  running the real thing -- this was checked carefully against fixture
  strings and still shipped broken, and was only caught once it actually
  ran inside the desktop through a pty and got watched draw, not just read
  from a saved dump.
- **A `local` statement's own later words cannot see its own earlier ones.**
  `local id=$1 b=${PZ[$id]["blank"]}` does not work -- `$id` in `b`'s value
  is expanded before `local` assigns anything, the same as real bash and
  dash, verified against both. It reads whatever `id` meant before this
  statement (usually empty, sometimes some enclosing function's `id` by
  coincidence of both being named `id`, which is worse: five instances of
  this shipped looking correct in Puzzle and Note Pad, because every call
  site happened to pass a same-named local down, until one was tested from
  a context with no enclosing `id` at all and an empty subscript surfaced
  the whole thing as `hibr: arithmetic: syntax error near ''`, several
  calls away from any of the five actual causes. `local id=$1` on its own
  line, then a second `local` statement for anything that reads `$id`,
  always works, the same as `mines_draw`'s own two-statement locals
  already do it.

## Testing discipline

- Tests with a `.expected` file are **recorded** (first line exit status, then
  stdout+stderr) because the behaviour is deliberately ours. Everything else is
  **compared against bash** on stdout and exit status.
- Recorded tests must be **deterministic**: no `$$`, ports or timings in output;
  order redirections so error text containing paths is suppressed
  (`cmd 2>/dev/null >file`, not `cmd >file 2>/dev/null`).
- In `self.hibr`, assert on `"${a[*]}"`, never `"${a[@]}"` — the latter splits
  into several arguments and the assertion silently never runs.
- Network tests pick a port from `$$`, use timeouts, and never `pkill -f` a
  pattern that can match the harness's own command line.
- Re-record an `.expected` file only after reading the diff and agreeing the
  new behaviour is correct.

## Owner's preferences for the work

- Ask before implementing when requirements are ambiguous, and offer choices.
- Show complete code, not fragments.
- Be honest about limitations and bugs; state costs of design choices plainly.
- Prioritise, in order: resident memory, startup time, loop throughput,
  fork/exec overhead.

## Open items

- Running real scripts **at execution level** is now done too, by
  `tests/corpus.py`: 465 invocations over 155 system scripts, run under both
  shells in a sandbox and compared on stdout and status. It went **48 differing
  to 7**, and not one of the nine fixes came from guessing -- each came from
  reading a difference: nested backquotes kept verbatim, `test` having no
  grammar beyond three argument counts, `read` neither assigning nor failing at
  end of file, `getopts` unable to bundle, `${@:2}` slicing text instead of
  selecting parameters, `$"..."` printing a dollar sign, an assignment
  discarding its substitution's status, `command -v` not existing, and a
  command substitution inheriting errexit. Keep using it; what is left is a
  short list, not a research project.
- The seven that remain, each needing its own read: `uz` (status 2 vs 1),
  `tzselect` with no arguments (status 1 vs 0, and bash prints an empty line),
  `byobu-ulevel --version` (no output from hibr), and
  `aptitude-run-state-bundle`, which differs only in the random suffix `mktemp`
  gave it -- harness noise that `corpus.py` should normalise the way it already
  normalises the sandbox path. `cc_aiwo_bootstrap.sh` is the owner's own script
  and **bash** is the one that fails it, with 127.
- Loop throughput is 1.2x behind dash on a `while` loop and 1.4x on `case`,
  from 1.7x and 1.6x. What is left is genuinely diffuse. Measure this with
  instruction counts, not the clock: the wall time between two separately
  built binaries moves several percent on code layout alone, and said +5.5%
  for a change that cost 0.23% of the instructions.
- All 63 test files are now leak-clean under ASan with `detect_leaks=1`. A
  forked child that `_exit`s still leaks whatever it held. Measure a leak change
  against the previous commit rather than against zero -- and against the other
  tests: the command cache's 96 bytes were found by noticing that only one test
  file reported anything, and then that `hash` reported the same number, which
  is what said cache rather than new code.
- hibr uses about 110 kB more than dash, and that is the binary rather than the
  heap: 29 kB of a 1792 kB resident set is heap, so there is no allocator work
  left that would move it. Shrinking it means less code. The README says so.
  Measure memory as a median of many runs; the spread is about 180 kB.
- `set -S` stays opt-in, measured in `docs/adr/0009`. It now changes a working
  script in exactly one way -- an expansion stops splitting -- since the empty
  rule was aligned with zsh. The remaining 31% is scripts that meant to split.
- Prompt status divergences from git, all deliberate: renames are matched only
  on identical content, submodule working trees are not inspected, and `**` in
  the middle of a gitignore pattern behaves as `*`.
- The desktop's idle CPU was measured, not assumed: two real, long-running
  sessions cost 1.6-2.2% of one core with nothing open and nothing forked,
  almost entirely from `dt_draw` and `console flush`'s grid diff running on
  every `DT_TICK` regardless of whether anything changed -- `console key MS`
  already returns the instant a key or a `SIGWINCH` arrives, so a short tick
  bought nothing for responsiveness and only paid for it in wakeups. Raising
  the default from 200 to 2000 cut both sessions' idle cost to 0.15-0.2% with
  no test changes, since `tests/desktop.py` and `tests/apps.py` already set
  their own short `DT_TICK`. What is left, if it is worth more than this:
  skip `dt_draw` entirely when nothing is dirty and block indefinitely
  instead of on a tick, waking only for input, resize, or a computed
  next-needed-time (an app's `dt_want`, the bar clock, `dt_deskscan`'s
  interval); and `about.hibr`'s CPU/memory refresh, which relies on being
  drawn every default tick to notice its own 3-second throttle has elapsed
  rather than calling `dt_want`, would need converting first or it silently
  refreshes late under a long default tick.
- The core binary is 358 KB stripped, 15,369 lines across `src/*.c` -- both
  the README and this file's own opening line had drifted stale (313 KB,
  ~13,000/~12,000 lines) before being re-measured and corrected. Per-file
  text size, compiled separately with `tcc -c`: `expand.c` 52.6K, `exec.c`
  36.7K, `bi.c` 31.4K, `edit.c` 20.2K, `daily.c` 19.0K, `lex.c` 19.0K,
  `net.c` 15.9K, `parse.c` 15.8K, the rest under 13K each. Nothing
  desktop-related is in `src/*.c` at all -- console, term, pty and hold are
  already modules, loaded on demand, and all 14 `.so` files together total
  476K outside the core. What is core but arguably language-adjacent rather
  than shell-core by this file's own "grows only for what makes it a better
  shell" test -- `net.c`, `json.c`, `text.c`, `args.c`, the interactive-only
  `edit.c` -- are also exactly the features the README leads with as reasons
  to use hibr over dash; moving any of them to a module is a product
  decision about what "just works out of the binary" means, not a cleanup,
  and has not been made.
