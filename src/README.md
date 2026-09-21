# src — the shell

Everything that makes up the `hibr` binary. Each file owns one concern, and
the boundaries are real: the lexer does not know about execution, the expander
does not know about the parser's node types.

| file | role |
|---|---|
| `mem.c` | Arenas with mark and release, `str`, `vec`, pools, and `lg` logging |
| `var.c` | The variable table; ordered nested maps (`ent`). Arrays are maps |
| `lex.c` | Tokens, quoting, `$…` parts, heredocs, `((…))`, `$'…'` |
| `parse.c` | Recursive descent into an arena-allocated AST |
| `expand.c` | Word expansion, splitting, globbing, braces, arithmetic |
| `exec.c` | Execution, redirection, pipelines, functions, `[[ ]]`, loops |
| `bi.c` | The sorted builtin table and most builtins |
| `job.c` | Job control, process groups, terminal handover |
| `trap.c` | Traps, `fail` and `try`, `printf` |
| `edit.c` | The line editor — UTF-8, wrapping, history, completion, `!!` |
| `daily.c` | Aliases, dirstack, prompt, `.hibrc`, `command`, `time` |
| `net.c` | Sockets, `/dev/tcp|udp|tls|unix`, scheme registration, `listen` |
| `json.c` | JSON parsing, emission and querying over the map model |
| `text.c` | The `str` and `arr` builtins |
| `regex.c` | `match` and `rsub` over POSIX extended regular expressions |
| `args.c` | `opt` and `args` declared argument parsing, and `title` |
| `mod.c` | Module loading and the module search path |
| `main.c` | Startup, the interactive loop, the prompt hook, teardown |

## The model, in brief

**Memory.** `sh.ar` holds the AST. `sh.xa` holds expansion results and is
marked and released once per command, so loops do not grow. A function body
keeps its arena by moving it to `sh.held`. Scratch `str` and `vec` come from
pools rather than `malloc`.

**Words.** A `word` is a list of `part`s — `P_TXT` with a quote flag, `P_VAR`,
`P_CMD`, `P_ARI`, `P_PSUB`. Expansion produces the text plus a per-byte quote
*mask*, and splitting and globbing act only on unquoted bytes.

**Variables.** A `var` holds a scalar and optionally an ordered map of `ent`,
each of which is a scalar or another map. `ty` carries JSON types; `am` marks a
map that may be empty.

**The result slot.** `ret` sets `$RET`. `x := cmd` clears the slot, sets
`sh.bind` so builtins fill it silently instead of printing, runs the command,
and binds the result. Nothing forks —
[why](../docs/adr/0005-results-travel-in-a-slot.md).

**The builtin table is sorted** and searched with `bsearch`. Adding a builtin
means keeping it in order; `tests/260-builtins.t` fails if a name stops
resolving.

Coding conventions and the traps already found are in
[CLAUDE.md](../CLAUDE.md).
