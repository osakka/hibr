# examples

Complete programs, not fragments. Each one runs, and each uses several
features together rather than demonstrating a single builtin.

| | |
|---|---|
| [`httpd.hibr`](httpd.hibr) | A static web server. Parses requests with regex into maps, reads files through a redirection, and answers — without forking once |
| [`fetch.hibr`](fetch.hibr) | An HTTP and HTTPS client that queries JSON responses. No curl, no jq |
| [`ls-report.hibr`](ls-report.hibr) | Loads a module, uses it through the result slot, and summarises a source tree with declared arguments, maps, regex and JSON |
| [`hibrc`](hibrc) | A starter `~/.hibrc`, which is what `deploy.sh` writes if you do not already have one |

## httpd

```
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

```
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

```
hibr examples/httpd.hibr --root . --count 1 --quiet &
hibr examples/fetch.hibr --query .shell http://127.0.0.1:8080/api.json
```

## A note on map keys

Both HTTP examples fold `-` to `_` in header names before using them as map
keys. A subscript containing an operator is evaluated arithmetically, so
`head[content-type]` reads as subtraction rather than as a key. See
[decision 0006](../docs/adr/0006-arrays-are-sparse-maps.md).

---

For the pieces in isolation see [the language documentation](../docs/language.md);
for scripts that exercise edge cases rather than read well, see [`tests/`](../tests/).
