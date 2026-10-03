# dav — a WebDAV client

`dav` lists, fetches and changes files on a WebDAV server: Nextcloud,
ownCloud, a NAS, Apache's or nginx's DAV, rclone, Fastmail -- anything that
speaks RFC 4918. It is its own HTTP/1.1 client, with no curl and no
libraries: connections are kept per server and reused, TLS goes through the
shell's own relay (libssl `dlopen`ed on first use), and a login is Basic or
Digest (MD5, MD5-sess), whichever the server asks for.

```text
mod load dav                        # or need dav
dav server set cloud https://cloud.example.com/remote.php/dav/files/me \
    -u me -p 'app-password'
dav ls dav://cloud/Documents        # d|f, size, modified (epoch), name
r := dav ls dav://cloud/Documents   # ${r[0]["name"]} ["dir"] ["size"] ["mtime"] ["etag"] ["type"]
dav get dav://cloud/Documents/notes.md          # to ./notes.md
dav put report.pdf dav://cloud/Documents/       # keeps the name
dav get -r dav://cloud/Photos ./Photos          # a whole folder
dav mkdir dav://cloud/New; dav rm dav://cloud/Old
dav mv dav://cloud/a.txt dav://cloud/Archive/a.txt
```

## Locations

`dav://NAME/path` names a server set up with `dav server set`; the path is
written plainly and encoded on the way out. A full `http://` or `https://`
address works too, already encoded, with the login of the server whose
address it begins with.

## Commands

| command | does |
|---|---|
| `dav ls LOC` | what a folder holds, a line each: `d` or `f`, size, modified (seconds since the epoch), name; with `:=` a map per entry |
| `dav stat LOC` | one file or folder, the same fields; with `:=` one map |
| `dav get [-r] LOC [DEST\|-]` | download, to a file named after it unless told; `-` is standard output; `-r` a folder and everything in it |
| `dav put [-r] [-m ETAG] [-n] PATH LOC` | upload; a location ending in `/` keeps the file's name; `-m` only if the file still has that ETag, `-n` only if nothing is there; the new ETag is the result |
| `dav mkdir LOC`, `dav rm LOC` | make a folder, remove a file or folder |
| `dav mv [-f] LOC LOC`, `dav cp [-f] LOC LOC` | on the server, within one server; nothing is put over something already there unless `-f` |
| `dav test NAME` | whether the server can be reached and logged in to |
| `dav servers` | every server: name, address, user, whether its certificate is checked; with `:=` a map by name, with `haspass` -- never the password |
| `dav server set NAME URL [-u USER] [-p PASS] [-k\|-K]` | add a server or change one; what is not given stays as it was; `-k` stops checking its certificate (a NAS with its own), `-K` starts again |
| `dav server rm NAME`, `dav server rename OLD NEW` | |
| `dav close` | close every connection and forget every login learned |

`$DAV_CODE` holds the last HTTP status (0 when nothing answered), so a
script can tell a conflict (412) from a missing file (404) from a refused
login (401) without reading the message.

## Where servers are kept

`$XDG_CONFIG_HOME/hibr/dav` (`~/.config/hibr/dav`), or `HIBR_DAV_CONF`: a
line per server, tab-separated, written by `dav server` with mode 600 and
replaced in one step. Passwords are in it as they are -- hiding them would
only pretend -- so a file anyone else can read is refused outright.

## How it behaves

- **A kept connection that went stale is redialled**, once, when the
  server closed it while idle; a server that closes every connection works
  as well, just slower.
- **Redirects are followed**, five at most -- a folder named without its
  slash is the usual one. A redirect to another place goes there without
  the login.
- **A name in a listing that could step outside its folder is never
  listed or written**: empty, `.`, `..`, or one holding a slash once
  decoded. That is what keeps a hostile server from making `get -r` write
  anywhere else.
- **A conditional upload checks first and asks too**: `-m` and `-n` look at
  the file's ETag before sending and send `If-Match`/`If-None-Match`, since
  some servers (rclone's among them) ignore the headers.
- **A download goes to `NAME.part` and is renamed** only once it is whole.
- A forked child -- a background job, a pipeline -- never uses a
  connection its parent holds; it dials its own.
- Timeouts are 30 seconds for connecting and for each read, or
  `HIBR_DAV_TIMEOUT` seconds.

## What it does not do

- No locking (`LOCK`/`UNLOCK`), no properties of your own (`PROPPATCH`).
- Digest with SHA-256 is not spoken; MD5 Digest and Basic are. A server
  that only offers Bearer, NTLM or Negotiate is refused, by name.
- Folders are copied between two servers with `get -r` and `put -r`
  through a local folder; `mv` and `cp` work within one server.
- It is refused under `--plan`: it reaches the network.

`tests/dav.py` runs it against `tests/davserve.py`, a WebDAV server of the
suite's own that can be any of the above shapes, and it was checked by hand
against rclone's `serve webdav`.
