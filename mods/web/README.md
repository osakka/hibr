# web — a browser, headless Chromium drawn as cells

`web` runs Chromium (or Chrome) headless and drives it over its own
DevTools protocol, on a pipe -- `--remote-debugging-pipe`, so there is no
port, no WebSocket and nothing listening -- and turns each page into cells:
the page's own text, placed where the browser laid it out, in its colours
and weights, over a half-block picture of its backgrounds and images.
browsh had the idea; this is the same picture with no Go process and no
WebSocket between.

```text
mod load web                    # or need web
t := web open https://example.com
web wait $t                     # until it has loaded (15 s at most)
web title $t; web url $t
web text $t                     # the page's text, as a reader would copy it
r := web links $t               # ${r[0]["href"]} ${r[0]["text"]}
web eval $t 'document.querySelectorAll("p").length'
web click $t 4 25               # a cell, row and column of the page
web type $t 'hello'; web key $t enter
web back $t; web forward $t; web reload $t
web size $t 24 80; web render $t; web draw $t 1 1 -p w3
web close $t; web quit
```

## Commands

| command | does |
|---|---|
| `web open [url]` | a new tab, blank or at an address; gives its number |
| `web close T` | close a tab |
| `web tabs` | every tab's number |
| `web go T url` | go to an address |
| `web back T`, `web forward T`, `web reload T` | history |
| `web wait T [ms]` | wait until the page has loaded; status 1 when it has not by then |
| `web url T`, `web title T` | where the tab is, and what the page calls itself |
| `web text T` | the page's visible text (`innerText`) |
| `web links T` | every link: a line each, address, tab, text; with `:=` a map `r[i]["href"]`, `r[i]["text"]` |
| `web eval T js` | run JavaScript in the page; a string as it is, anything else as JSON; a thrown error is said, status 1 |
| `web size T rows cols` | lay the page out for that many cells |
| `web render T` | take a frame: the text, the fields and the picture |
| `web draw T row col [h w] [-p pane]` | draw the last frame on the display, into a pane if named |
| `web click T row col` | press and release at a cell |
| `web wheel T rows` | scroll, down with a positive number |
| `web key T name` | a key by the console's name for it: `enter`, `backspace`, `tab`, `up`, `pagedown`, `shift-tab`, `ctrl-a`, a letter |
| `web type T text` | text into whatever has the focus |
| `web loading T`, `web dirty T`, `web focus T` | status 0 while loading, when the page has changed since the last frame, when a field has the keyboard |
| `web tap T` | copy everything the tab's pages hand a media source: installed before each page's own scripts, it notes each buffer as video or audio by its type and keeps every chunk appended to it |
| `web take T [VFD\|- [AFD\|-]]` | write what the tap has kept since last asked -- video to one descriptor, audio to the other -- and say what happened: `v BYTES a BYTES reset 0\|1 vtype TYPE atype TYPE`; a reset is a new media source (the next video, an ad break), after which both streams start again from their init segments |
| `web tapseek T` | after a seek: drop what was kept and keep each buffer's init segment again, so what is taken next reads from its start |
| `web fd` | the browser's pipe, for `console watch`, so a desktop wakes when a page changes |
| `web poll` | act on whatever the browser has said; status 0 if a tab wants a new frame |
| `web quit` | stop the browser |

## How a frame is made

The page is laid out at 8 by 16 CSS pixels a cell. `render` asks the page
for every visible character's place, colour, weight and link (a range per
character, so a proportional font still lands in the right cells; text on
one line is kept together, and a later piece on the same row goes on after
an earlier one rather than over it), and for every form field's value, then
takes a screenshot of the visible part at one pixel a column and two a row
-- with the page's text made transparent for that moment, so the picture
holds backgrounds and images and no letters. Each cell of the picture is a
half block of its two pixels; the text goes over it, on the average of the
two, in its own colour unless that would be unreadable there. A character
two columns wide takes two cells. A link's cells carry its address, so a
terminal that does links (OSC 8) makes them clickable.

A dialog the page opens (`alert`, `confirm`) is accepted at once.

## Taking a page's media

`web tap` is how the YouTube app plays YouTube: the page's own player
fetches, at whatever quality it is told, and each chunk it appends to its
media source is copied as it goes in -- ordinary fragmented MP4 or WebM,
which the media module decodes (`media feed` and `media pipe`). Nothing
about it is YouTube's: any page that plays through a media source can be
taken from the same way.

## Where things are kept

The browser keeps its cookies and history in a profile:
`HIBR_WEB_PROFILE`, else `~/.local/share/hibr/web/profile`. If another
Chromium already holds that one, this shell's browser gets a temporary
profile of its own, removed when it stops. `HIBR_WEB_BROWSER` names the
browser to run, else the first of `chromium`, `chromium-browser`,
`google-chrome`, `google-chrome-stable`, `chrome` on `PATH`, else Chrome or
Chromium in `/Applications` on macOS; `HIBR_WEB_ARGS` adds flags to it.

## What it does not do

- **No JavaScript of its own on the page beyond what it asks.** Pages run
  as they would in Chromium; what is drawn is a snapshot taken when asked,
  so an animation moves only as often as frames are taken.
- **Text is placed, not reflowed.** A page whose text is much larger or
  smaller than a cell keeps its layout and loses spacing; a heading set
  huge is drawn as plain characters where it starts.
- **No downloads, file uploads, audio or video.**
- It loads only by name under `--plan`: a browser reaches the network,
  which a plan refuses.
