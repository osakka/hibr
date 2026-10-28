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
| [`play.hibr`](play.hibr) | A terminal video player around the media module: the picture as half blocks or ASCII, the sound through the machine's device, from a file or an address |
| [`hibrc`](hibrc) | A starter `~/.hibrc`: aliases, the prompt module and its powerline options. `deploy.sh` writes a starter of its own, much the same, when you have none |

The products that used to live here moved to [`programs/`](../programs/) in
0.99.211: the desktop (`programs/desktop/`), `vw` (`programs/vw.hibr`, ADR
0036) and the catalogue (`programs/catalogue.hibr`, whose command was `apps`
until 0.99.195; see [the catalogue](../docs/catalogue.md)).

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

## play

```text
hibr examples/play.hibr movie.mp4
hibr examples/play.hibr -m ascii -s 90 https://example.com/clip.webm
```

The whole player is the window around the [media module](../mods/media/README.md):
`media open`, `media size`, then `media frame`, `media draw` and `media next`
in a loop that sleeps until the next frame is due. Space pauses, left and
right seek five seconds, up and down change the volume, `m` cycles half
blocks, ASCII and plain ASCII, and `q` or escape leaves. It needs FFmpeg's libraries (libavformat 59 to 62) on the
machine; nothing is linked.
