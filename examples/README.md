# examples

Complete programs, not fragments. Each one runs, and each uses several
features together rather than demonstrating a single builtin.

| | |
|---|---|
| [`httpd.hibr`](httpd.hibr) | A static web server. Parses requests with regex into maps, reads files through a redirection, and answers — without forking once |
| [`conf.hibr`](conf.hibr) | Reads an INI file into nested maps in one pass, then queries it or emits it as JSON. No sed, no awk, no jq |
| [`workers.hibr`](workers.hibr) | A pool of named coprocesses, handed work round robin and reached through indirect expansion |
| [`fetch.hibr`](fetch.hibr) | An HTTP and HTTPS client that queries JSON responses. No curl, no jq |
| [`ls-report.hibr`](ls-report.hibr) | Loads a module, uses it through the result slot, and summarises a source tree with declared arguments, maps, regex and JSON |
| [`traceroute.hibr`](traceroute.hibr) | Traces a route with the trace module, places each hop from its name and the zone table, and draws the route on a world map; `--demo` draws a made-up one |
| [`console-demo.hibr`](console-demo.hibr) | A small full-console program: panes, colour, decoded keys, and a redraw that costs only what changed |
| [`vw.hibr`](vw.hibr) | A Bitwarden and Vaultwarden client, installed as the `vw` command: logs in, syncs and reads a vault that stays encrypted on this machine, so unlocking needs no network (ADR 0036) |
| [`apps.hibr`](apps.hibr) | The desktop's application manager, installed as the `apps` command: fetches an index over HTTPS, verifies a bundle's `sha256`, and unpacks it into the folder a scan already looks in (ADR 0041) |
| [`desktop/`](desktop/README.md) | The desktop: a window manager written in hibr, and the apps, desk accessories and control panel built on it |
| [`hibrc`](hibrc) | A starter `~/.hibrc`, which is what `deploy.sh` writes if you do not already have one |

## httpd

```text
hibr examples/httpd.hibr --root ./docs --port 8080
hibr examples/httpd.hibr --root ./docs --count 1 --quiet   # serve once and stop
```

Serves a directory, with content types by extension, directory indexes, `HEAD`,
and `403` for paths that try to walk upwards. `listen` runs the handler inside
the shell, so the handler sees every function and map declared above it, and
`recv -a` reads the file through a descriptor rather than shelling out to
`cat`. Nothing forks per request.

Its one real limitation: a response body travels through a shell variable, so
files containing NUL bytes will be truncated. It is a file server for text —
HTML, CSS, JavaScript, JSON, Markdown — not for images. `--fork` gives one
process per connection if you want isolation instead.

## fetch

```text
hibr examples/fetch.hibr https://example.com/
hibr examples/fetch.hibr --query .slideshow.title https://httpbin.org/json
hibr examples/fetch.hibr --status http://127.0.0.1:8080/missing
hibr examples/fetch.hibr -i -H 'Accept: application/json' https://api.example.com/v1
```

`http` and `https` differ only in which `/dev` path the redirection opens.
Certificates are verified by default, and libssl is loaded on the first TLS
connection and never before — an invocation that only speaks plain HTTP pays
nothing for the capability.

The two together make a round trip with no external programs involved:

```text
echo '{"shell":"hibr"}' > api.json
hibr examples/httpd.hibr --root . --count 1 --quiet &
hibr examples/fetch.hibr --query .shell http://127.0.0.1:8080/api.json
```

That prints `hibr`.

## A note on map keys

Both HTTP examples fold `-` to `_` in header names before using them as map
keys. A subscript containing an operator is evaluated arithmetically, so
`head[content-type]` reads as subtraction rather than as a key; a quoted one,
`head["content-type"]`, is always a literal key. See
[decision 0006](../docs/adr/0006-arrays-are-sparse-maps.md).

---

For the pieces in isolation see [the language documentation](../docs/language.md);
for scripts that exercise edge cases rather than read well, see [`tests/`](../tests/).

## apps

```text
apps list [TEXT]            what the index offers, or what matches TEXT
apps installed              what is installed here
apps info NAME              everything the index says about one
apps install NAME...        fetch, verify and place
apps remove [-f] NAME...    take one away
apps update [NAME...]       everything with a newer version, or these
apps index [URL]            the index this fetches from, or set it
```

The desktop's application manager, and a worked example of why *an app is a
folder of plain files* (ADR 0041): installing one is placing a file where a
scan already looks. Each of the desktop's five scan lists begins with a
folder of your own and has a data folder after it, so

```text
~/.config/hibr/apps/calc.hibr      yours, and it wins
~/.local/share/hibr/apps/calc.hibr what `apps install` placed
```

means patching an installed app is copying it to your own folder and editing
it there, and an update cannot overwrite your copy.

Three things about trust, named separately because they protect different
things: **TLS** gives the identity of the server the index came from,
the index's **`sha256`** gives the integrity of the bundle, and **nothing**
gives the identity of the author — there are no signatures, and `apps info`
says so rather than implying otherwise.

A bundle carrying a `.so` is refused with that word: native code has no
sandbox, and root loads modules only from the folder compiled into the
binary (ADR 0016). A member whose path climbs out of its own folder is
refused naming the path. An app with a window open is not removed — the
manager asks the running desktop over its own control socket and says which
windows are open, because removing it would leave the desktop calling
functions with nothing behind them.

No module: `dav get` fetches (binary-safe to a file, where `dav request`
NUL-terminates its answer), `json` reads the index, `archive` looks inside a
bundle before anything is unpacked, and `mkdir`, `rm` and `mv` place it.
