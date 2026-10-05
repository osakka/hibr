# mods — modules

A module is a shared object exporting one `hibr_module` symbol. It can add
builtins, and it can add *protocols* — once a scheme is registered,
`/dev/<name>/…` works anywhere a filename does.

```c
#include "hibr.h"

int m_hello(sh *s, int ac, char **av) { hibr_ret(s, "hello"); return HIBR_OK; }

const hibr_bi hello_bi[] = { { "hello", m_hello, "say hello" }, HIBR_BI_END };
HIBR_MODULE("hello", "1.0", "greeting builtins", hello_bi, 0, 0);
```

```text
tcc -Iinclude -shared -o build/mods/hello.so mods/hello.c
hibr -c 'mod load ./build/mods/hello.so; mod list; mod drop hello'
```

| module | what it is |
|---|---|
| `sys.c` | A minimal example — `drop`, `epoch`, `sleepms`, `state`, `upper` |
| `http.c` | A **scheme**: registers `/dev/http/host/port/path`, so an HTTP body can be read by anything that reads a file |
| `ls.c` | An in-process `ls` with columns, `-l -a -A -h -t -S -r -d -1 -F` and colour, whose listing also lands in `$RET` |
| `math.c` | `math`: floating point where `$(( ))` has only integers -- `+ - * / % ^`, comparisons, `? :`, `round sqrt abs pow ...`, shell variables by name, and `sum avg min max count` over arrays; shown as a spreadsheet shows numbers. One of the two modules a `--plan` may load |
| `csv.c` | `csv`: CSV as RFC 4180 says -- quoted fields, doubled quotes, line breaks inside quotes, CRLF or LF, a byte-order mark skipped, any separator (`-s ';'`, `-s tab`). `csv read FILE` (or `-t TEXT`) binds every record as `r[i][j]`, or with `-H` as `r[i][name]` from the first; built in one pass, so 50,000 rows bind in a quarter of a second. `csv open`/`row`/`close` stream a record at a time; `csv line FIELD...` writes one, quoting only what needs it; `csv split TEXT` one record's fields. Sheet imports and exports through it |
| `lines.c` | `lines`: files by line, exactly, for whoever edits this tree from a shell (Gitea #67). `lines show [-n] FILE FROM [TO|$]` prints a range byte for byte; `lines grep [-F] [-i] [-l] [-A n] [-B n] [-C n] [-g glob] PATTERN [PATH...]` searches a tree in name order, hidden folders and binary files skipped, printing grep's `file:line:text` and `--` between context groups, as fast as GNU grep on a plain pattern; `lines count FILE TEXT` counts a literal; `lines edit [-n] FILE` reads `<<<<` old `====` new `>>>>` blocks from standard input and applies them in order, each old text found exactly once or nothing written, through a new file renamed over the old with its mode kept. Output is printed, never bound into `$RET` |
| `uni/` | `uni`: text for a cell grid, every table from one Unicode version (`tools/unigen.py`, 15.1.0). `uni vis [-d ltr|rtl|auto] TEXT` is the display order -- UAX #9, every case of Unicode's BidiTest and BidiCharacterTest passing (`tests/uni_bidi.py`) -- with Arabic shaped to presentation forms, lam-alef as one cell, marks after their base and mirrored brackets, checked against FriBidi (`tests/uni_shape.py`); `uni shape` shapes in logical order; `uni width`, `uni class`, `uni version`; `uni levels [-c]` reads `dir;input` lines for the conformance tests. ADR 0030 |
| `salat/` | `salat`: prayer times for a place and a day -- the sun's position as Meeus gives it, the method (angles, intervals, offsets) read from a JSON file, Asr school, high-latitude rule and a person's own minutes on top -- see [salat/README.md](salat/README.md), ADR 0035 |
| `hcal/` | `hcal`: calendar systems as data -- the Hijri date of a day in Umm al-Qura, the tabular calendars or one added as a JSON file, and back, adjusted by whole days for sighting -- see [hcal/README.md](hcal/README.md), ADR 0034 |
| `vw/` | `vwk`: a Bitwarden or Vaultwarden vault's keys and crypto, kept in memory and never printed -- PBKDF2 or Argon2id, EncStrings, session and PIN wrapping, TOTP, libcrypto `dlopen`ed; the `vw` command (`examples/vw.hibr`) logs in, syncs and reads the vault offline through it -- see [vw/README.md](vw/README.md), ADR 0036 |
| `lang.c` | `lang`: a translation catalogue (`lang load FILE`, one JSON file a language) in a hash; `lang get KEY [ARG...]` with `%s` and `%N$s` filled so a translation can reorder, `lang plural N KEY [ARG...]` by CLDR's integer rules, `has`, `info`, `keys`, `off`. The desktop's `dt_tr` and `dt_trn` are built on it -- ADR 0032 |
| `prompt/` | A segmented prompt, and a native reader for git's object store |
| `lint/` | The rules behind `hibr --explain`: reads a parsed script and names the mistakes in it, without running it. It adds no builtin; it offers `"lint"` (`mods/lint.h`), which `--explain` asks for |
| `auth.c` | `auth check`: whether PAM takes a password for a user, with libpam opened on first use and no privilege of its own -- what the desktop's screen lock asks; `auth whoami` for the login and real name; `auth open`/`run`/`close`, a PAM session and a command run in it as the user, for the login screen |
| `darwin.c` | macOS only: `cpu` and `mem` from the kernel's own counters, with no fork |
| `console/` | The text display: alternate screen, a cell grid redrawn by damage, panes, decoded keys; offers `"display"` (`mods/display.h`) — [README](console/README.md) |
| `cat/` | `cat`, byte-identical in a pipe and coloured on a terminal; offers `"highlight"` (`mods/highlight.h`) — [README](cat/README.md) |
| `most/` | A pager on the display interface — [README](most/README.md) |
| `hvi/` | hibr's vi: gap buffer, lazy line index, linear undo — [README](hvi/README.md) |
| `mon/` | A system monitor over `/proc` — [README](mon/README.md) |
| `sysinfo/` | What the machine is, with a picture — [README](sysinfo/README.md) |
| `trace/` | Unprivileged traceroute over UDP, one-shot and live — [README](trace/README.md) |
| `pty/` | Pseudo terminals: run a program on one and drive it; offers `"pty"` (`mods/pty.h`) — [README](pty/README.md) |
| `term/` | A terminal emulator, a program's screen as cells; offers `"terminal"` (`mods/term.h`) — [README](term/README.md) |
| `hold/` | Sessions that outlive their terminal: detach, log off, attach again; offers `"hold"` — [README](hold/README.md) |
| `img/` | Decode an image and draw it as cells, libpng opened on first use — [README](img/README.md) |
| `db/` | A small column store: typed columns, appended rows changed and deleted in place, filters and aggregates, zone maps — [README](db/README.md) |
| `dav/` | A WebDAV client: its own HTTP/1.1 with kept connections, Basic and Digest logins, TLS through the shell's relay, RFC 4918 listings; servers in a private list -- [README](dav/README.md) |
| `media/` | Video and sound: FFmpeg's libraries loaded at run time, a decoding thread and a sound thread, ALSA or AudioQueue, the picture as half blocks or ASCII following the sound's clock -- [README](media/README.md) |
| `web/` | A web browser: headless Chromium driven over its own pipe, each page drawn as cells -- text where it was laid out, over a half-block picture; tabs, links, forms, scripting -- [README](web/README.md) |
| `md/` | Markdown as CommonMark and GitHub write it: HTML, or a style per character for a program that draws it; every spec example passes — [README](md/README.md) |
| `html/` | HTML as the WHATWG standard parses it, every html5lib tree-construction case passing; CSS selector queries, and pages laid out as lines with a style per character, links and picture boxes — [README](html/README.md) |
| `pim/` | Calendars and contacts as their files are written: iCalendar and vCard read and made, recurrence expanded over any window (every RFC 5545 example matches dateutil), zones through zoneinfo, invitations answered — [README](pim/README.md) |
| `email/` | IMAP (with IDLE and Gmail's labels), POP3 and SMTP, accounts in a private file, TLS through the shell's relay, and MIME: RFC 2047 headers, quoted-printable, base64, charsets, parts, building — [README](email/README.md) |

## Naming

The shell is linked `-rdynamic`, so a module function whose name the shell also
exports is preempted by the shell's. `m_` belongs to `src/mod.c`; a module uses
its own prefix (`sy_`, `pr_`). A collision is silent until the call, and then it
is a crash, so it is worth checking:

```text
nm -D build/mods/x.so | awk '$2=="T"{print $3}' | sort -u |
comm -12 - <(nm -D build/hibr | awk '$2=="T"{print $3}' | sort -u)
```

## Giving up root

`drop user[:group]` in `sys.c` gives up root for good: supplementary groups
through `initgroups`, then `setresgid` and `setresuid` so the saved ids go too,
then a check that the ids took and that `setuid(0)` fails. It refuses unless
the effective uid is 0, and if it fails once anything has changed it ends the
shell rather than continue half dropped.

Two patterns. `listen -b` binds without serving, so nothing stays root:

<!-- not run: binding port 80 needs root -->
```sh
mod load sys
listen -b 80 LFD
drop www-data
while accept $LFD C; do serve <&$C >&$C; exec {C}<&-; done
```

Or `listen -f`, which forks before running its handler, so the handler drops
and the accepting parent stays root — the inetd arrangement:

<!-- not run: a sketch -- serve's body is elided, and port 80 needs root -->
```sh
serve() { drop www-data; ... }
listen -f 80 serve
```

See [0016](../docs/adr/0016-privileges-are-dropped-never-gained.md).

## prompt/

The largest module, built by its own Makefile rule from every file in the
directory, because it contains a working subset of git.

| file | role |
|---|---|
| `prompt.c` | Module entry, the `prompt` builtin, and its diagnostics |
| `fmt.c` | The `$var` / `[text](style)` / `(conditional)` format language, and styles to ANSI |
| `seg.c` | The segment table and every segment that is not git |
| `git.c` | Repository discovery, HEAD, refs, repository state, configuration |
| `inflate.c` | DEFLATE — RFC 1951, and the RFC 1950 zlib wrapper |
| `obj.c` | Loose objects, pack indexes v1 and v2, `OFS_DELTA` and `REF_DELTA` chains, alternates |
| `sha1.c` | SHA-1, and git's blob hashing rules including symlinks |
| `idx.c` | `.git/index` versions 2, 3 and 4, and the cached-tree extension |
| `ign.c` | `.gitignore` matching, where `*` does not cross a slash |
| `status.c` | Working-tree comparison, tree diff against HEAD, the untracked walk |
| `walk.c` | Distance from the upstream branch, by walking both histories by date |

Why this is written out rather than shelling out to `git`:
[0014](../docs/adr/0014-read-git-objects-natively.md).

Full detail in [the prompt documentation](../docs/prompt.md) and
[the module ABI](../docs/modules.md).
