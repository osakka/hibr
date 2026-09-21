# CLAUDE.md

Context for working on **hibr** — a small, fast, bash-flavoured shell in C,
built with tcc. Read this first; it records the conventions, the design
decisions and the traps already found, so they don't get rediscovered.

## What hibr is

A shell that runs a useful subset of bash syntax in ~12k lines and ~270 KB,
with a resident footprint below dash and roughly half of bash, and faster than
bash on tight loops. It is *not* a drop-in bash replacement and must not be
described as one: some divergences are deliberate (see Design decisions).

Its reasons to exist beyond size: nested maps, JSON with type fidelity, regex
capture, in-process text/array operations, native TCP/TLS/Unix sockets and
servers, typed function signatures, result slots (`x := f` without forking),
declared CLI arguments, and a module ABI that lets modules add *protocols*
(`/dev/<name>/…`), not just commands.

Version and ABI: `HIBR_VER` and `HIBR_ABI` in `include/hibr.h` (0.21, ABI 7).

## Build and test

    make                 # tcc; builds ./build/hibr and mods/*.so
    make TLS=0           # compile TLS out entirely
    make check           # = tests/run.sh
    make install         # PREFIX=/usr/local, modules to $(PREFIX)/lib/hibr
    ./build/hibr -n script      # parse only

    tests/run.sh [-v] [prefix]           # C-side harness, 43 tests
    ./build/hibr tests/self.hibr                 # suite written in hibr, 84 assertions
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
| `include/hibr.h` | public types, macros and the module ABI (v7) |
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
| `mods/prompt/` | the prompt module, including a native reader for git's object store — see `mods/README.md` for the file-by-file breakdown |

Each directory carries its own `README.md` with the detail: `src/`, `include/`,
`mods/`, `tests/`, `examples/`. User-facing documentation is under `docs/`, and
every deliberate divergence from bash has a record in `docs/adr/`. Keep those
current — this table is a summary, not the source of truth.

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
9. **TLS verifies certificates by default**; `HIBR_TLS_INSECURE=1` to disable.
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
- **New `var`/`ent` records must be zeroed** — uninitialised map pointers crash.
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
- **`al_quote` escapes, it does not quote.** The arguments it re-emits have
  already been expanded, so wrapping them in `'…'` would mark every byte as
  quoted and make a subscript reached through an alias literal —
  `alias u=unset; u a[i]` would stop resolving `i`. Per-character escaping
  protects the metacharacters without masking the subscript's own bytes. An
  argument that is empty or holds a newline still has to be quoted, since
  `\<newline>` is a line continuation.
- **Argument masks are gated on the command name.** `xargv` builds them only
  when the first expanded word names a builtin in `bi_mask`, so an ordinary
  command pays one `strcmp` and nothing else — building them unconditionally
  cost 7% on tight loops. A builtin that grows an interest in its arguments'
  quoting has to be added to that list, and `command` must keep forwarding
  `sh.amask + 1` with its shifted `argv`.
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
- **Two fast paths carry the loop, and both must stay honest.** `xargv` skips
  brace expansion, `xwm`, splitting and globbing for a word that is one
  unquoted run of text with no byte in `w_meta`; `xwm` skips building the
  string and mask pair for a bare `$name` with no operator, subscript or quote
  whose value has no `w_meta` byte either. Both bail out to the slow path on
  anything else, both are off under `set -S` and `set -u`, and together they
  are most of a 30% loop gain. Anything added to expansion has to be reachable
  from the slow path, or the fast path has to learn to refuse it.
- **`qsort` is not given a NULL base.** An empty directory leaves `vec.p` NULL,
  and glibc declares the argument non-null, which UBSan reports.
- **`ob_hex` does not check what follows the digits**, because in `packed-refs`
  an object name is followed by a space. Callers check the length.

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

- Run real scripts under hibr **at execution level** and fix by frequency. The
  parse-level sweep is done: 169 system scripts, all of them parse. Comparing
  what they *do* against bash is where the rest is, and it is the method that
  found `${u:?}` not guarding, `trap EXIT` not firing and `TZ=UTC` doing
  nothing.
- Loop throughput is 1.21x behind dash, from 1.71x. What is left is genuinely
  diffuse and the next step is not obvious; stop before the fast paths grow
  cases that the slow path would have handled correctly.
- Shells leak on the way out, about 3.8 kB, and a forked child that `_exit`s
  leaks whatever it held. Both predate this work; measure a leak change against
  the previous commit rather than against zero.
- hibr uses about 110 kB more than dash, and that is the binary rather than the
  heap: 29 kB of a 1792 kB resident set is heap, so there is no allocator work
  left that would move it. Shrinking it means less code. The README says so.
  Measure memory as a median of many runs; the spread is about 180 kB.
- A differential fuzzer: `tests/fuzz.py` checks the parser does not crash, but
  every real bug this year came from comparing behaviour, not from parsing.
- Decide whether `set -S` should become the default.
- Possibly: `declare -l`/`-u`, `trap RETURN`, `select` refinements, a `plan N`
  count in `self.hibr`.
- A quoted subscript does not survive an alias: `al_quote` escapes rather than
  quotes, so `alias u=unset; u h["a-b"]` evaluates the subscript. Fixing it
  needs the alias path to carry masks rather than re-lex text.
- `export`, `read` and `[[ -v ]]` do not parse a subscript at all, quoted or
  not — `export e[k]=v` is silently inert. The argv quote mask (`sh.amask`) is
  already there for whichever of them should grow one; see `docs/adr/0006`.
- No right-hand or transient prompt; both need `ed_draw` work.
- Prompt status divergences from git, all deliberate: renames are matched only
  on identical content, submodule working trees are not inspected, and `**` in
  the middle of a gitignore pattern behaves as `*`.
