# The prompt

`PS1` with backslash escapes still works and is still the default. Beyond it,
an interactive shell will build its prompt by calling a command in process, so
a prompt can be assembled without forking anything:

```
PROMPT_FN=myprompt          # a function, a builtin or a module builtin
fn myprompt() { ret "$(id -un) $PWD> "; }
```

Whatever the command leaves in the result slot becomes the prompt, verbatim —
it is not run through `\u`-style escapes or word expansion again, so a branch
name containing `$` or a backslash is safe. Two variables are set for it:
`$STATUS`, the status of the last command, and `$DURATION`, how long that
command took in milliseconds.

## The prompt module

`mods/prompt.so` builds a prompt out of segments, in the spirit of starship but
with no configuration file, no second binary and no forks — the configuration
lives in an ordinary nested map.

```
mod load prompt             # found on $HIBR_MODPATH, or where make install put it
PROMPT[format]='$dir$git$duration$status$char'
PROMPT[dir][style]='bold cyan'
PROMPT[git][symbol]='on '
PROMPT[char][symbol]='>'
```

Loading the module points `PROMPT_FN` at itself, so a `.hibrc` that loads it
gets a working prompt immediately. `prompt list` names every segment, and
`prompt render` prints one prompt for inspection.

**Format strings.** `$name` names a segment at the top level, or one of that
segment's fields inside it. `[text](style)` styles a run of text. `(text)`
is conditional: it disappears unless some variable inside it produced a value.
`\$`, `\[`, `\]`, `\(`, `\)` and `\\` are literal, and `\n`, `\t` and `\e`
are what they look like. A multi-line prompt works; the line editor accounts
for it.

**Styles** are space-separated words: `bold`, `dimmed`, `italic`, `underline`,
`blink`, `inverted`, `strikethrough`, `none`, a colour name (`black red green
yellow blue purple cyan white`, each with a `bright-` form), a `#rrggbb`
triple, or a number for the 256-colour palette. `fg:` and `bg:` choose which
side; a bare colour is the foreground. Styles nest, and the enclosing style is
restored after an inner one ends.

**Colour.** Every count in the git segment carries its own style, so the
prompt stays scannable at a glance rather than being one block of colour:

| count | default | count | default |
|---|---|---|---|
| `staged_style` | bold green | `untracked_style` | bold cyan |
| `modified_style` | bold yellow | `conflicted_style` | bold red |
| `deleted_style` | bold red | `renamed_style` | bold blue |
| `ahead_style` | bold green | `stash_style` | bold purple |
| `behind_style` | bold red | `state_style` | bold yellow |

Set any of them to `none` for a monochrome prompt, or to anything the style
grammar accepts.

**Configuration** is `PROMPT[segment][key]`. Every segment takes `format`,
`style`, `symbol` and `disabled`; any other key is readable from the segment's
format as a variable, so `PROMPT[dir][read_only_style]='bold red'` is picked up
by `$read_only_style` with no code behind it.

| segment | shows | notable keys |
|---|---|---|
| `dir` | working directory, relative to the repository root when inside one | `truncate`, `truncation_symbol`, `repo_root` |
| `git` | branch or short commit, repository state, working-tree status, and distance from the upstream branch | `untracked`, `staged`, `track`, `max_walk` |
| `char` | the prompt character, red after a failure | `error_style` |
| `status` | a non-zero exit status | |
| `duration` | how long the last command took | `min` (default 2000 ms) |
| `jobs` | background jobs | `threshold` |
| `user`, `host` | shown over ssh or as root | `show_always`, `full` |
| `venv`, `conda`, `nix`, `aws`, `docker`, `kube` | the environment you are working in | |
| `os`, `container`, `battery`, `memory`, `shlvl`, `time` | the machine | `threshold`, `time_format` |
| `rust`, `node`, `golang`, `python`, `c` | a marker file for that language is present | |

`time`, `os`, `memory`, `kube` and `shlvl` are off unless you enable them, and
default symbols are plain text rather than icons, because the line editor
measures the prompt and a glyph your font renders at a different width would
move the cursor. Set `PROMPT[git][symbol]` and friends to Nerd Font characters
if your terminal has them.

Everything above is read from files and environment variables, so a prompt
costs no processes.

**Reading git's objects.** The module carries its own DEFLATE decoder and
object store, so it can read commits, trees, tags and blobs without zlib,
without `git` and without forking: loose objects, pack indexes v1 and v2, delta
chains through both `OFS_DELTA` and `REF_DELTA`, and `info/alternates`. Linked
worktrees work too: the `.git` file and `commondir` are followed back to the
shared object store.

```
prompt object <sha>          # type and size
prompt object -p <sha>       # the object's contents
```

Objects are read lazily and bounded — `PROMPT[git][max_object]` (4 MB by
default) caps how far one will inflate, so a crafted object cannot make your
prompt allocate hundreds of megabytes. Object names are not re-verified against
their contents, the same as git on an ordinary read.

**Working-tree status.** The `git` segment reports what `git status` would,
computed in process: `.git/index` is parsed (versions 2, 3 and 4), every entry
is compared against the working tree — by stat data first, and by hashing the
file when the stat data is inconclusive, which is git's own racy-index rule —
the index is diffed against HEAD's tree for what is staged, and the working
tree is walked for untracked files, honouring `.gitignore` at every level,
`.git/info/exclude` and `core.excludesFile`. Distance from the upstream branch
comes from walking both histories by commit date until they meet.

```
main ↑2 ↓1 +1 !1 ?1
```

Each count has its own field and symbol — `$staged` `$modified` `$deleted`
`$untracked` `$conflicted` `$renamed` `$stash` `$ahead` `$behind`, and the
combined `$status` and `$ahead_behind` — so the layout is yours. On a branch
with no commits yet, everything in the index counts as staged, as git does.
A file moved without being edited is one rename, not an add and a delete.

Three switches decide what it costs: `PROMPT[git][untracked]`,
`PROMPT[git][staged]` and `PROMPT[git][track]`, all on by default, and
`PROMPT[git][max_walk]` (512) caps the commit walk — past that the distance is
simply not shown.

**What it costs.** In a 2,600-file repository a full prompt takes about 35 ms,
against 32 ms for `git status --porcelain=v2 --branch` alone — and hibr pays no
fork on top. Walking for untracked files is most of that for both: turning
`PROMPT[git][untracked]` off takes the prompt to about 11 ms. Small
repositories are a few milliseconds, and a directory outside any repository is
about 1.4 ms.

**Where it differs from git.** Renames are matched only when the content is
byte-identical, so an edited-and-moved file still counts as two changes. A
submodule is clean unless its directory is missing; its own working tree is not
inspected. In a `.gitignore` pattern, `**` in the middle of a path is treated
as `*`; leading `**/` and trailing `/**` work as documented.

What is not there yet: a right-hand or transient prompt, which needs work in
the line editor.

---

[← documentation index](README.md) · [← project README](../README.md)
