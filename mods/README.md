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
| `http.c` | A **scheme**: registers `/dev/http/host/port/path`, so an HTTP body can be read by anything that reads a file; the request is the fetch module's, the body kept in an unlinked file. Also `msum` (the sum of a map's values) and `oops` (fail with a message from a module) |
| `ls.c` | An in-process `ls` with columns, `-l -a -A -h -t -S -r -d -1 -F` and colour (`-G` forces it), whose listing also lands in `$RET`; `-q` prints nothing and only fills it, which is also what happens under `:=` |
| `math.c` | `math`: floating point where `$(( ))` has only integers -- `+ - * / % ^`, comparisons, `? :`, `round sqrt abs pow ...`, shell variables by name, and `sum avg min max count` over arrays; shown as a spreadsheet shows numbers. One of the two modules a `--plan` may load |
| `copy.c` | `copy`: copying and moving trees with progress and a clean stop, for Files' copy window (Gitea #402). `copy run [-m] [-i ms] [-s statusfile] [-k stopfile] DST SRC...` counts what it has to do, then copies each source into the folder DST under a hidden `.NAME.hibr-part` renamed when whole -- modes, owners where it may, times and symbolic links kept, nothing followed -- printing `total`, an `ok` or `err` line per source and `done` or `stopped`; the status file holds one line, `copy BYTES TOTAL ITEMS ITEMSTOTAL NAME`, replaced whole at most once every `-i` ms. A stop file appearing, or SIGTERM, ends it with the item in hand removed. `-m` moves: a rename where it can be, and across filesystems a copy and then the original removed, only once its copy is whole. `copy rename DST SRC...` does the renames alone and answers what is left. `HIBR_COPYSLOW` (ms per 64 KB) and `HIBR_COPYEXDEV` are the suites' knobs |
| `procs.c`, `procs.h` | the one reader of the process table under `/proc`, compiled into `proctab` and `mon` alike, the way `gfxask.c` is shared: per process its CPU ticks, real uid, VmRSS and its name, recovered from `argv[0]` where the kernel cut it. `mon` read the table on its own until 0.99.400, its memory from `stat`'s field that proc(5) calls inaccurate |
| `contain.c` | `contain`: a program that may read and compute but not act on the world (Gitea #432, ADR 0052). `contain exec DIR -- CMD` closes every descriptor above 2, contains its own process and execs: on Linux, Landlock (read and execute anywhere, every write under `DIR` and `/dev/null`, TCP refused at ABI 4, signals and abstract sockets scoped at ABI 6) and a seccomp filter refusing sockets, tracing, io_uring, every change of mode, owner, times and extended attributes, and below ABI 6 any signal to a process but itself; on macOS, `sandbox_init` with a profile saying the same. A failure once descriptors are closed ends the process with 126 -- it never runs the command uncontained. `contain probe` answers `landlock N` or `sandbox`, and fails where there is nothing. Sheet does not trust that answer alone: it contains a test write inside and outside the folder once per desktop before calling its formulas contained |
| `frames.c` | `frames`: the socket a window's picture is sent to, as Blit's own (Gitea #486, Blit ADR 0223). `frames serve` listens on an abstract SOCK_SEQPACKET socket with a token, takes Blit's hello at version 5, the only one since Blit 0.3.74, and answers it with what every display imports (the console's `dpframes` `can`), and passes FRM3 and FRM1 frames to the console under the stream's number; `FRAME release` and `FRAME error` come back rewritten into the producer's id. `frames place` from a window's drawing keeps the stream's picture in a pane. The sockets are hooks in the console's wait, so frames pass without the script running. A memfd of RGB is mapped only where nothing takes frames by descriptor, and drawn with `dp->image`; a dmabuf is never mapped. The producer's input route, `INPUT sock <id> <path>` (the gamescope bridge's `INPUT eis` went with it in 0.99.483 and is refused), goes up the same way to every display's Blit, its answers back in the producer's id; whether the route is on is the window's to say (`frames input N on|off` from its `_focus`), so a producer's own `INPUT on|off` is left alone; and how a hold takes the pointer is the window's too (`frames input N hold confine|relative`, Blit 0.3.70), said again by the console with the route and replayed by `hold` to each display. A socket served with `-w` is a compositor's -- blit-compositor, Blit ADR 0228, Gitea #494: its hello is answered `window=1` (and `drag=1` and `inhibit=1`, the last saying a program's `WINDOW inhibit ID on|off` is honoured, Blit #171), each window is a number of its own under the server, its WINDOW lines are queued for the desktop (`frames events`) in those numbers and the desktop's go back down (`frames window`; the displays and the primary, which name no window, as `frames layout N NAME X Y W H MHZ [PCT]` -- the seventh word a display's Program Scale, Blit #170, which the desktop says only to a compositor at 0.3.130 or later -- `frames layout N end` and `frames primary N NAME`, Gitea #514), its input route is asked for by socket (`INPUT sock`, which Blit writes to directly), and a popup's picture joins its window's route (`frames input W member`). A picture's `WINDOW view` (Blit #159, wp_viewporter's crop and scale) is kept on it: a top-level, popup or drag icon is placed as the view's rectangle of its buffer, composed with its geometry (`sx + gx * sw / dw`), scaled into its pane; `frames info` says the size it is shown at (`vwidth`, `vheight`) beside its frame's, which the desktop sizes it by, and `view`; the view is news for the desktop and goes to the console, which says it to Blit as `INPUT view` after the picture's route. A subsurface's view goes to the console too, which draws the subsurface through it and says it only where its Blit takes a placement's size in pixels (0.3.127, `W=`/`T=`); held, hold does both per display. Every word's place on the wire is read in `fl_wsplit` and written in `fl_wfmt`: version 5's one grammar, the picture's id always the third word, so a relay rewrites ids by one rule |
| `dbus.c` | `dbus`: the session bus for the desktop's tray (Gitea #505), libdbus-1 dlopen'd and its few types declared by hand. Opening takes no name: asked (`tray on`), it is the session's StatusNotifierWatcher when nobody else is, and a host either way, and `tray off` lets both names go at once, with the items -- so a desktop without the tray is no tray a program could register with and hide in. Programs' tray items register with it, by bus name as Qt does or by object path as libappindicator does, and `item N` reads each one: title, id, status, icon name, picture, tooltip, menu. `menu N fetch` sends AboutToShow and reads the com.canonical.dbusmenu layout; the rows (`menu N`) come back with mnemonics gone and invisible rows left out. `click`, `activate`, `secondary`, `context` and `scroll` go back to the program, and `pixmap N FILE [#bg]` writes its picture for `img`: with a colour, a PPM laid over it; without, a PNG with its own alpha, for `img draw -a` to blend over a see-through bar (it links `png.c` and the deflate it needs). **It never waits on the bus.** Hello, RequestName, AddMatch, every property read and every menu are sent and their replies taken up by `pump`, which also answers what happened (`add`, `change`, `gone`, `menu`, `menustale`, `nomenu`, `noactivate`, `more`, `down`); the desktop runs it when the watched descriptor (`open`, `fd`) wakes it. The connection is a private one: no exit when the bus goes away, SIGPIPE left as it was, the descriptor close-on-exec, and never an autolaunch. A bus whose queue is full is refused before libdbus's own connect, which has no timeout, would wait. `DBUS_SESSION_BUS_ADDRESS` set and empty means no bus, which is how the suites keep a test desktop off the machine's own. Text from the bus is cut and stripped of control bytes, pictures, items and menus are bounded, and a call the desktop does not answer gets an error at once. Since 0.99.531 it is also the session's notification server when asked (`notes on`, `notes off`): a program's Notify is kept and said as `note N`, read with `note N` (app, summary, body as plain text with the spec's markup gone, urgency, actions), and answered with `noteact N KEY` or `noteclose N REASON`, the program told by ActionInvoked and NotificationClosed; a note its program replaces keeps its number, one it closes is `noteclose N`. And it is the session's org.freedesktop.ScreenSaver when asked (`awake on`, `awake off`, Blit #171), at `/org/freedesktop/ScreenSaver` and `/ScreenSaver` both: a program's Inhibit is answered with a cookie and kept -- by cookie and the bus name that asked, at most `BU_MAXINH`, one more refused rather than one taken away -- until UnInhibit or until that name leaves the bus; each change is `awake N`, the count, and `awakes` lists them (`COOKIE APP: REASON`), which the desktop's saver asks at the moment it would start; any other ScreenSaver method is refused at once. `tests/999-dbus-awake.t` drives it against its own dbus-daemon. `tests/traymod.py` drives it against its own dbus-daemon and `tests/trayitem.py`, a stand-in item and notifying program |
| `dirwatch.c` | `dirwatch`: folders and files watched by the kernel through inotify, so a change wakes whoever watches its descriptor with `console watch`, and nothing has to poll. `add PATH` answers a watch number (one folder is one watch, whoever asks), `drop N` ends it, `fd` is the descriptor, `read` drains what happened as the list of watches it happened under -- each once, a queue overflow counting as every watch, and a dropped watch's own end left out -- and `list` names them. Files is the first user. Linux only: elsewhere every verb says so and fails, and a caller keeps its own fallback |
| `varwatch.c` | `varwatch`: which watched variables changed, told by the variable table itself (ADR 0050). The core asks the module before every write to a variable carrying `A_WATCH` -- in `v_assign`, `v_del`, `v_setp`, `v_delp`, `v_path` when it creates, and where `json parse`, `NAME=()` and `:=` graft a map on after the assignment -- and says whether the write changes anything, so a write of the same value is no change. `add NAME` / `drop NAME` watch a global (kept across unset and re-declaration; a local shadowing it is never reported), `seq` is a number that moves on a change, `moved` lists what changed since last asked (`NAME<TAB>KEY`, KEY empty for the whole, a variable with more than 32 keys changed counting as whole), `list` the names, `guard on` refuses every write to a watched variable but those made between `apply on` and `apply off` (an app's process holding the desktop's state as a read-only copy), `asks` how many writes the table asked about. An unwatched write costs the core 4 instructions (9 subscripted); `tests/999-varwatch.t` drives 41 write forms and fails with any hook taken out |
| `proctab.c` | `proctab`: every process under `/proc` in one call, a line each -- `pid utime stime uid rss name`: clock ticks from `stat` (counted from the last `)`, since a name may hold one), the real uid and VmRSS in kB from `status` (0 for a kernel thread), and `comm` extended from `argv[0]`'s base name when `comm` sits at the kernel's 15-byte limit and `argv[0]` begins with it. A process that goes between two reads is left out, never half read. Task Manager's scan, which read all of this in the shell and stopped the desktop for 200 to 350 ms once a second on a busy machine. Fails where there is no `/proc`, and Task Manager then uses `ps` |
| `csv.c` | `csv`: CSV as RFC 4180 says -- quoted fields, doubled quotes, line breaks inside quotes, CRLF or LF, a byte-order mark skipped, any separator (`-s ';'`, `-s tab`). `csv read FILE` (or `-t TEXT`) binds every record as `r[i][j]`, or with `-H` as `r[i][name]` from the first; built in one pass, so 50,000 rows bind in a quarter of a second. `csv open`/`row`/`close` stream a record at a time; `csv line FIELD...` writes one, quoting only what needs it; `csv split TEXT` one record's fields. Sheet imports and exports through it |
| `lines.c` | `lines`: files by line, exactly, for whoever edits this tree from a shell (Gitea #67). `lines show [-n] FILE FROM [TO|$]` prints a range byte for byte; `lines grep [-F] [-i] [-l] [-A n] [-B n] [-C n] [-g glob] PATTERN [PATH...]` searches a tree in name order, hidden folders and binary files skipped, printing grep's `file:line:text` and `--` between context groups, as fast as GNU grep on a plain pattern; `lines count FILE TEXT` counts a literal; `lines edit [-n] FILE` reads `<<<<` old `====` new `>>>>` blocks from standard input and applies them in order, each old text found exactly once or nothing written, through a new file renamed over the old with its mode kept. Output is printed, never bound into `$RET` |
| `uni/` | `uni`: text for a cell grid, offering `"uni"` (`mods/uni.h`) to other modules, every table from one Unicode version (`tools/unigen.py`, 15.1.0). `uni vis [-d ltr|rtl|auto] TEXT` is the display order -- UAX #9, every case of Unicode's BidiTest and BidiCharacterTest passing (`tests/uni_bidi.py`) -- with Arabic shaped to presentation forms, lam-alef as one cell, marks after their base and mirrored brackets, checked against FriBidi (`tests/uni_shape.py`); `uni shape` shapes in logical order; `uni width`, `uni class`, `uni version`; `uni map [-d ltr|rtl|auto] TEXT FROM TO` lays characters FROM up to TO out as one line of TEXT's paragraph and answers the display order, each character's column and each drawn character's source and column, failing with nothing set when TEXT holds no right-to-left; `uni levels [-c]` reads `dir;input` lines for the conformance tests. ADR 0030 |
| `salat/` | `salat`: prayer times for a place and a day -- the sun's position as Meeus gives it, the method (angles, intervals, offsets) read from a JSON file, Asr school, high-latitude rule and a person's own minutes on top -- see [salat/README.md](salat/README.md), ADR 0035 |
| `hcal/` | `hcal`: calendar systems as data -- the Hijri date of a day in Umm al-Qura, the tabular calendars or one added as a JSON file, and back, adjusted by whole days for sighting -- see [hcal/README.md](hcal/README.md), ADR 0034 |
| `archive/` | `archive`: a tarball, a zip or a disc image as a folder -- `.tar` and `.tar.gz` indexed at open, zips and ISO 9660 images (or a CUE sheet and its BIN) read only, `archive ls|stat|cat`, and `/dev/archive/NAME/path` anywhere a filename goes, through the scheme mechanism rather than a kernel mount; folders that the archive never declared are implied from the member names; `archive extract` checks every name before writing any, and `archive create` writes a tar or tar.gz; the inflate is `mods/inflate.c`, shared with the prompt and fetch modules -- see [archive/README.md](archive/README.md), Gitea #102 |
| `mermaid/` | `mermaid`: Mermaid diagrams as cells -- flowcharts laid out the way dagre does it (cycles broken, ranks by longest path, bend points on long edges, the median heuristic, and a track per edge in each gutter so two never merge into one line), sequence diagrams as columns and rows, pies as proportional bars; `render` answers rows **and the style of every character** as runs, so a window can draw it in its own theme's colours, and everything it does not understand is named rather than half-drawn -- see [mermaid/README.md](mermaid/README.md), Gitea #140 |
| `pdf/` | `pdf`: PDF documents through Poppler, every library `dlopen`ed: a viewport of the laid-out document as a PPM for `img draw`, so a page is pixels, half blocks or characters as the terminal allows; text, search, selection, links and the outline, every position in points from a page's top left or pixels of the laid-out document -- see [pdf/README.md](pdf/README.md), Gitea #348 |
| `vw/` | `vwk`: a Bitwarden or Vaultwarden vault's keys and crypto, kept in memory and never printed -- PBKDF2 or Argon2id, EncStrings, session and PIN wrapping, TOTP, libcrypto `dlopen`ed; the `vw` command (`programs/vw.hibr`) logs in, syncs and reads the vault offline through it -- see [vw/README.md](vw/README.md), ADR 0036 |
| `lang.c` | `lang`: a translation catalogue (`lang load FILE`, one JSON file a language) in a hash, and `lang add FILE` for an app's own -- the same language, refused whole if broken, never replacing a key already there (0.99.523); `lang get KEY [ARG...]` with `%s` and `%N$s` filled so a translation can reorder, `lang plural N KEY [ARG...]` by CLDR's integer rules, `has`, `info`, `keys`, `off`. The desktop's `dt_tr` and `dt_trn` are built on it -- ADR 0032 |
| `prompt/` | A segmented prompt, and a native reader for git's object store |
| `lint/` | The rules behind `hibr --explain`: reads a parsed script and names the mistakes in it, without running it. It adds no builtin; it offers `"lint"` (`mods/lint.h`), which `--explain` asks for |
| `inspect/` | `inspect functions\|calls\|refs\|vars\|check FILE`: what a script defines, calls, names and reads or writes, as records with the exact source span, from the parser's own view, never running anything; offers `"inspect"` (`mods/inspect/inspect.h`). [docs/inspect.md](../docs/inspect.md) is the contract, ADR 0056 -- [README](inspect/README.md) |
| `auth.c` | `auth check`: whether PAM takes a password for a user, with libpam opened on first use and no privilege of its own -- what the desktop's screen lock asks; `auth whoami` for the login and real name; `auth service` for the PAM service a check asks when none is named; `auth ready` loads libpam and fails where there is none; `auth open`/`run`/`close`, a PAM session and a command run in it as the user, for the login screen |
| `darwin.c` | macOS only: `darwin cpu` and `darwin mem` from the kernel's own counters, with no fork |
| `console/` | The text display: alternate screen, a cell grid redrawn by damage, panes, decoded keys; offers `"display"`, `"scroll"`, `"dpover"` and `"dpframes"` (`mods/display.h`); carries `mods/deflate.c` and `mods/png.c` to compress a still picture's payload before it is sent (`console imgcomp`, Gitea #129) — [README](console/README.md) |
| `cat/` | `cat`, byte-identical in a pipe and coloured on a terminal; offers `"highlight"` (`mods/highlight.h`) — [README](cat/README.md) |
| `most/` | A pager on the display interface — [README](most/README.md) |
| `hvi/` | hibr's vi: gap buffer, lazy line index, linear undo — [README](hvi/README.md) |
| `mon/` | A system monitor over `/proc` — [README](mon/README.md) |
| `sysinfo/` | What the machine is, with a picture — [README](sysinfo/README.md) |
| `trace/` | Unprivileged traceroute over UDP, one-shot and live — [README](trace/README.md) |
| `pty/` | Pseudo terminals: run a program on one and drive it; offers `"pty"` (`mods/pty.h`); a program another process owns, asked for over a channel and its master handed across (ADR 0050) — [README](pty/README.md) |
| `zygote.c` | `zygote`: what a desktop's one prepared host for app processes needs and a script cannot do (ADR 0050). `pair` makes a connected channel, `send`/`recv` carry a message with descriptors on it, `close` lets one go; `fork` makes a fresh process with its own `$$`, a session of its own, its own random numbers and no descriptor but the standard three and those it keeps (`-k`), `reaped` says how each such child ended, `kids` which are left, and `end` signals one only while it is still this process's unreaped child. The runtime's `--zygote` loop (`programs/desktop/runtime.hibr`) is its one user; `tests/999-farpty.t` and `tests/zygote.py` drive it |
| `fdmsg.c`, `fdmsg.h` | Not a module: messages with descriptors over a Unix stream pair -- a header of length and descriptor count, the descriptors on its first byte -- compiled into `pty` and `zygote`. A stream and not `SOCK_SEQPACKET`, whose close with unread data resets the peer before it reads what is queued, and which macOS has no Unix form of; descriptors arrive close-on-exec; and `fm_closeall`, which closes everything but a list with no allocation, for a child forked from a process that may hold threads |
| `term/` | A terminal emulator, a program's screen as cells; offers `"terminal"` (`mods/term.h`) — [README](term/README.md) |
| `hold/` | Sessions that outlive their terminal: detach, log off, attach again; offers `"hold"` and `"holdframes"` (`mods/blitfr.h`) — [README](hold/README.md) |
| `gfxask.c`, `gfxask.h` | Not a module: what a terminal says it can draw, asked once and parsed in one place -- the kitty graphics query and primary device attributes for sixel -- compiled into `console`, `hold` and `prompt` |
| `b64.c`, `b64.h` | Not a module: base64 both ways, compiled into `console`, `term`, `email`, `fetch`, `vw` and `web`, which each wrote their own until 0.99.433 (nine copies, Gitea #356). The decoder takes the standard and the URL-safe alphabet alike, passes over line breaks, stops at `=`, and skips anything else -- or with `strict` refuses the text |
| `jsondir.c`, `jsondir.h` | Not a module: a catalogue of JSON files -- every `*.json` in the folders of a variable, the person's own folder and the shipped one, each file's text handed to the module's own parser -- compiled into `hcal` and `salat`, which each wrote it out until 0.99.434 |
| `htmlent.c`, `htmlent.h` | Not a module: the HTML5 named character references, generated by `tools/htmlents.py` from WHATWG's `entities.json` as one blob of names and characters with an offset each -- no relocations, where a table of pointers had two per entry -- and the one lookup, `he_find`; compiled into `html` and `md`, which each carried the whole table until Gitea #355 |
| `blitfr.c`, `blitfr.h` | Not a module: picture frames handed to Blit by descriptor (Blit ADR 0202), a sealed memfd over an abstract `SOCK_SEQPACKET` socket, compiled into `console` and `hold`; Linux only |
| `boxscale.c`, `boxscale.h` | Not a module: a rectangle of an RGB bitmap box-filtered to a size, so a picture can be cropped to the screen rather than squashed into it; compiled into `console`, `img` and `web`, which each wrote it out until 0.99.435 (Gitea #356) |
| `inflate.c`, `inflate.h`, `deflate.c`, `inftab.c`, `png.c`, `png.h` | Not a module: DEFLATE both ways with its zlib and gzip wrappers, written here rather than linked against libz, and PNG's chunks, CRC-32 and filters around it. `inflate.c` is compiled into `prompt`, `fetch` and `archive`; `deflate.c` into `console`, `img`, `archive` and `dbus`; `inftab.c`, the tables both read, into all of them; `png.c` into `console`, `img` and `dbus` |
| `img/` | Decode an image and draw it as cells, libpng and libturbojpeg opened on first use — [README](img/README.md) |
| `db/` | A small column store: typed columns, appended rows changed and deleted in place, filters and aggregates, zone maps — [README](db/README.md) |
| `dav/` | A WebDAV client: RFC 4918 listings, transfers and conditional uploads over the fetch module's HTTP; servers in a private list -- [README](dav/README.md) |
| `fetch/` | An HTTP client for scripts and modules: kept connections, codings, redirects, logins, proxies and JSON cookie jars; the `fetch` builtin, and offers `"fetch"` (`mods/fetch.h`) -- [README](fetch/README.md) |
| `media/` | Video and sound: FFmpeg's libraries loaded at run time, a decoding thread and a sound thread, a sound server through libpulse-simple or else ALSA or AudioQueue, the picture as half blocks or ASCII following the sound's clock -- [README](media/README.md) |
| `web/` | A web browser: headless Chromium driven over its own pipe, each page drawn as cells -- text where it was laid out, over a half-block picture; tabs, links, forms, scripting -- [README](web/README.md) |
| `md/` | Markdown as CommonMark and GitHub write it: HTML, or a style per character for a program that draws it; every spec example passes — [README](md/README.md) |
| `html/` | HTML as the WHATWG standard parses it, every html5lib tree-construction case passing; CSS selector queries, and pages laid out as lines with a style per character, links and picture boxes — [README](html/README.md) |
| `pim/` | Calendars and contacts as their files are written: iCalendar and vCard read and made, recurrence expanded over any window (every RFC 5545 example matches dateutil), zones through zoneinfo, invitations answered — [README](pim/README.md) |
| `email/` | IMAP (with IDLE and Gmail's labels), POP3 and SMTP, accounts in a private file, TLS through the shell's relay, and MIME: RFC 2047 headers, quoted-printable, base64, charsets, parts, building — [README](email/README.md) |

## A module's own version

`HIBR_MODULE`'s second field is the module's **own** version, and it has
nothing to do with the shell's. Until 0.99.125 eight modules declared
`HIBR_VER` there -- so their version changed on every release whether they
had changed or not, and `mod list` printed a column saying only which shell
you were running -- and eleven more carried the release they were first
written in (`0.21`, `0.22`, `0.24`), which is the same mistake in a slower
form (Gitea #162).

The rule: **`1.0` once a module does what its own README says, `0.x` while
it does not**, and it moves when the module's behaviour changes in a way a
caller could notice -- never on a release that did not touch it. So `dav`,
`db`, `media`, `mermaid`, `pdf` and `web` are below 1.0, each with a list of
what it cannot do yet in its README, as are a few newer ones; `mod avail`
prints every module's version.

Nothing compares it, which is what makes it free to mean that. A module
whose **`abi`** does not match the shell's is refused at load, and an
**interface's** own number -- `DP_API_VER`, `PY_API_VER`, `TM_API_VER` --
is what `hibr_require` matches exactly, so a provider and a user cannot
disagree. The version is read only to be printed: `mod list`, the log line
at load, and the Modules app.

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
directory (with `../inflate.c`, `../inftab.c` and `../gfxask.c`), because it
contains a working subset of git.

| file | role |
|---|---|
| `prompt.c` | Module entry, the `prompt` builtin, and its diagnostics |
| `fmt.c` | The `$var` / `[text](style)` / `(conditional)` format language, and styles to ANSI |
| `seg.c` | The segment table and every segment that is not git |
| `git.c` | Repository discovery, HEAD, refs, repository state, configuration |
| `pl.c` | The powerline look (`PROMPT[look]`, `PROMPT[sep]`) and the glyph sets (`PROMPT[glyphs]`: nerd, blit, unicode, ascii or auto), Gitea #360 |
| `pr.h` | The module's internal declarations |
| `../inflate.c` | DEFLATE — RFC 1951, and the RFC 1950 zlib wrapper, shared with the archive and fetch modules. Expanding only; `../deflate.c` is the other direction and `../inftab.c` the tables both read |
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
