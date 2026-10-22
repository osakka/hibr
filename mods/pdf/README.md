# mods/pdf

PDF documents through Poppler, loaded at run time (Gitea #348). The PDF
Reader (`programs/desktop/apps/Office/pdfr.hibr`, in the catalogue) is
built on it.

libpoppler-glib, libcairo, libgobject and libglib are all `dlopen`ed the
first time a document is opened, never linked. A shell that opens no PDF
pays nothing, and a machine without Poppler still runs everything else.
`pdf ready` says whether they load, and the first failure names what to
install: `apt install libpoppler-glib8` on Debian, `brew install poppler`
on a Mac. The few struct layouts read across the boundary (a rectangle, a
link mapping, an action, a destination, a `GList`, a `GError`) are written
from Poppler 22.12's and GLib's own headers. Each is one a stable C ABI has
kept since Poppler 0.x.

| verb | what it does |
|---|---|
| `pdf ready` | status: can Poppler be loaded here |
| `pdf open FILE [password]` | open it; the id lands in `$RET` |
| `pdf close ID` | close it, and remove the last view it wrote |
| `pdf pages ID`, `pdf title ID`, `pdf size ID PAGE` | how many pages, the title, a page's size in points |
| `pdf zoom ID fitw\|fitp\|PCT W H` | the zoom a mode means for a viewport of W by H pixels, and its percentage (100 is 96 dots an inch) |
| `pdf view ID DIR W H ZOOM TOP LEFT [-b rrggbb] [-k\|-s\|-c PAGE x1 y1 x2 y2]...` | render the viewport, see below |
| `pdf at ID ZOOM VIEWW X Y` | the page under a pixel of the laid-out document, and the point on it |
| `pdf where ID ZOOM PAGE Y` | where a point of a page is in the laid-out document, rounded up so a jump lands on its page |
| `pdf text ID PAGE` | the page's text |
| `pdf find ID TEXT` | every match: `page x1 y1 x2 y2`, a line each |
| `pdf select ID PAGE x1 y1 x2 y2` | the text inside a rectangle |
| `pdf link ID PAGE X Y` | the link at a point: `page N Y` or `uri U`; fails where there is none |
| `pdf outline ID` | the contents, a line an entry: depth, page, title, tab-separated |

## Two coordinate systems, and no third

Every position given to or answered by a verb is one of two kinds. Points
on a page, with the origin at its **top left**: Poppler reports search
matches and link areas from the bottom, and the module turns them over, so
a caller never meets that. Or pixels in the **laid-out document**: every
page one under the next at the zoom, 12 pixels apart, each centred in the
widest page's width. `pdf at` and `pdf where` turn one into the other.
Cells are the caller's business: a cell is so many pixels, which the caller
knows and the module does not.

## The view

`pdf view` renders the W by H pixels of the laid-out document that start
TOP pixels down and LEFT pixels across. So a window that shows the foot of
one page and the head of the next is one picture, and continuous scrolling
is only a matter of where it starts.
- `-b` is the colour between and beside the pages.
- `-k` marks a rectangle of a page as a search match (yellow), `-c` as the
  current one (orange), `-s` as a selection (blue).

It writes a binary PPM under a name of its own in DIR, `pdf-ID-N.ppm`, and
removes the one before. The img module's caches go by a file's mtime in
whole seconds, so a view written over another of the same name could be
shown from a cache as the last one. It answers the file, the page being read
(the one a quarter of the way down the view), and the document's height and
width in pixels. The caller places the file with `img draw`, which is what
makes a page pixels, half blocks or characters, whichever the terminal can
take.

## Tests

`tests/pdfapp.py` writes its documents with `tests/pdfmake.py`, which
`qpdf --check` passes as written. It checks every verb against positions
worked out from the file by hand, then the reader through a pty as pixels,
as half blocks and as text. A box without libpoppler-glib skips it.

## What it cannot do yet

It is 0.1, and these are what stand between it and 1.0.
- **Passwords:** a password is taken by `pdf open`, but the reader has no
  way to ask for one, so an encrypted document fails to open there.
- **Search size:** `pdf find` searches every page before it answers, so a
  document of several hundred pages holds the desktop while it does.
- **Selection:** within one page; a drag across a page break selects
  nothing on the second.
- **Not read:** forms, annotations other than links, attachments and
  layers are not read at all.
- **Horizontal scroll:** the view takes a LEFT, but the reader moves only
  up and down, so a page zoomed wider than its window cannot be scrolled
  sideways.
