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
| `web mode T cells\|pixels\|text` | how a page is drawn: text over the colours behind it, the page itself as pixels where the terminal can paint them, or nothing at all -- `text` makes a render read nothing, for a caller drawing the page's words itself |
| `web set T js\|images\|dark on\|off` | what this tab's pages may do: run their scripts, fetch their pictures (refused by resource type, so an image with no extension is caught too), and be asked for their dark scheme (`prefers-color-scheme`). Per tab, so another program's tabs in the same browser keep theirs; scripts and pictures already loaded stay until the page loads again |
| `web set T zoom PCT` | lay the page out PCT percent larger or smaller, 25 to 500; the extraction, the picture and the text all follow |
| `web set T ua TEXT` | the user agent this tab's pages are told; empty puts back the module's own |
| `web set T lang TAGS` | the languages this tab's pages are asked for, as an `Accept-Language` list (`fr-FR,fr;q=0.9`), riding on the user-agent override, and the locale its scripts see from the first; empty is the browser's own |
| `web set T dnt on\|off` | Do Not Track and Global Privacy Control: `DNT: 1` and `Sec-GPC: 1` on every request, and `navigator.doNotTrack` and `navigator.globalPrivacyControl` for the page's scripts |
| `web set T block PATTERNS` | addresses this tab never fetches, `*` for anything, separated by spaces or commas; empty fetches everything |
| `web set T geo 'LAT LON'` | where this tab says it is when a page asks and location is allowed; empty is no position |
| `web set T notes on\|off` | a page's notifications kept for `web notes` rather than lost, since a headless browser has nowhere to show them |
| `web permit NAME allow\|refuse`, `web permit reset` | what any page may be given -- `geolocation`, `camera`, `microphone` or `notifications` -- for the whole browser, which is what the protocol offers; a headless browser can ask nobody, so asking is not an answer. `reset` forgets every grant |
| `web downloads DIR\|off` | downloads into DIR, for the whole browser, each told to `web downloaded` when it ends; `off` refuses them, as the browser does unless told |
| `web downloaded`, `web notes` | what has happened since the last asking, a line each, and forgotten once told: `done PATH` or `failed NAME` for a download, `TITLE BODY` for a notification, a tab between. Status 1 when there is nothing |
| `web pdf T PATH` | the tab's page printed to PATH as a PDF, backgrounds included |
| `web set T live on\|off` | have the browser say when this tab's page repaints, so a page that changes on its own is marked dirty and read again: a screencast at 32 pixels kept running only as that signal, each frame acknowledged, and a repaint a render itself causes (cells mode hides and shows the page's text) kept quiet for 200 ms after it |
| `web scroll T` | where the page is scrolled to, how tall it is and how much of it shows, in CSS pixels, from the last render: `top height shown` |
| `web config browser\|profile\|args VALUE` | what the browser is started with next, over `HIBR_WEB_BROWSER`, `HIBR_WEB_PROFILE` and `HIBR_WEB_ARGS`; empty puts the environment's back |
| `web clear cookies\|cache` | forget the whole profile's cookies, or its cache, starting the browser if it is not running |
| `web clear history [T...]` | empty the back and forward lists of the tabs named, or of every tab |
| `web click T row col` | press and release at a cell |
| `web wheel T rows` | scroll, down with a positive number |
| `web key T name` | a key by the console's name for it: `enter`, `backspace`, `tab`, `up`, `pagedown`, `shift-tab`, `ctrl-a`, a letter |
| `web type T text` | text into whatever has the focus |
| `web loading T`, `web dirty T`, `web focus T` | status 0 while loading, when the page has changed since the last frame, when a field has the keyboard |
| `web tap T` | copy everything the tab's pages hand a media source: installed before each page's own scripts, it notes each buffer as video or audio by its type and keeps every chunk appended to it |
| `web sound T off\|click\|load` | record what the tab plays, for `web take` to hand over as audio: from when the page first plays -- a video or audio element, or an AudioContext made (`click`, after the browser's own rule that sound waits for a gesture), as each page loads (`load`, given as a gesture, so the page may play unasked -- and what it was refused before that is played again), or not at all (`off`). See Sound below |
| `web take T [VFD\|- [AFD\|-]]` | write what the tap has kept since last asked -- video to one descriptor, audio to the other -- and say what happened: `v BYTES a BYTES reset 0\|1 vtype TYPE atype TYPE snd on\|quiet\|-`, `snd` being a recording tab's state; a reset is a new media source (the next video, an ad break): a take stops at one, and the reset comes alone on the next take, with the new streams -- from their init segments -- on the one after, so a player can be started afresh before any of them arrives |
| `web tapseek T` | after a seek: drop what was kept and keep each buffer's init segment again, so what is taken next reads from its start |
| `web fd` | the browser's pipe, for `console watch`, so a desktop wakes when a page changes |
| `web poll` | act on whatever the browser has said; status 0 if a tab wants a new frame |
| `web quit` | stop the browser |

## How a frame is made

`pixels` is for a terminal that can place them (the kitty graphics protocol
or sixel, whichever it speaks -- ADR 0037): the
screenshot is taken at the viewport's own size rather than at one pixel a
column, and drawn through the display's own `image` entry -- so the page
looks like the page, with its own fonts, pictures and layout. It has **no
text layer at all** in that mode, on purpose: a text cell paints its own
background, so anything drawn over the bitmap would box itself out of it.
Clicks and keys still reach the page, since those go by coordinate rather
than through the cells; what is lost is reading the page as cells, so no
copying text out of it and no link cells. The Browser app chooses from
Control Panel > Pictures, and the window's own chrome stays text either way.

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

`dirty` is cleared **before** a render's extraction rather than after it, and
that is not a detail: the extraction is a CDP round trip, the wait for its
reply pumps the pipe, and every event queued behind that reply is dispatched
on the way -- `Page.loadEventFired` among them, which sets `dirty` again.
Clearing it afterwards wiped the one thing saying the page just read was
already out of date, and a Browser window kept a blank new document for ever
(Gitea #182). A failed render puts the flag back.

So a caller that draws a page per frame should ask `dirty` again **after**
drawing: set then, it means an event arrived while the frame was being made,
and a settled page sends nothing further to wake anything with.

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
`web config` sets any of the three for the next start, ahead of the
environment, which is how Control Panel > Browser names them.

It is started without Chromium's automation flag, and every tab says it is
the browser's own Chrome rather than "HeadlessChrome" (`HIBR_WEB_UA` gives
another, or an empty one leaves the browser's). A page that sees a headless,
automated browser is free to treat it as a robot, and YouTube's did: its
player stopped about a minute into a video with "Something went wrong",
every time, on the videos it guards most.

## Sound

Chromium is started with `--auto-accept-this-tab-capture` and **without**
`--mute-audio` (Gitea #305): a muted browser's capture of a tab is
silence. `web sound` records a tab with the page's own
`getDisplayMedia` -- the tab itself, so sound from another origin is
caught, where an element's `captureStream()` throws `SecurityError` for
exactly the media every site keeps on a CDN, and Web Audio is caught
too -- with the voice processing off (left on, a steady tone sank two
decibels in three seconds) and `suppressLocalAudioPlayback`, so a tab
being recorded does not also play on the machine Chromium runs on. The
video track that call insists on is stopped at once. What comes out is
WebM/Opus in quarter seconds, a reset first, in the tap's own queue; the
recording pauses after two seconds of silence (`snd quiet`) and goes on
when there is sound again, so a tab that played something once does not
keep its reader taking from it for ever.

In `click` the recording starts when the page plays something, not on
the press itself: it used to start on the first press or key, inside the
page's own `pointerdown`, which swallowed about one click in three, and
it started a recording for a page with no sound at all.

## The browser's own life

Chromium is the module's child, and on Linux it is asked to end with the
process that started it (`PR_SET_PDEATHSIG`), so a desktop that dies does
not leave one running; one that misses that moment, its parent already
gone, ends itself. Each call waits `WB_WAITMS` for its answer. A browser
that misses two in a row, or the first of a session, is stopped rather
than waited on again, and none is started for `WB_RESTMS` (30 s) after it
is stopped or fails to start: every call meanwhile fails at once. A
Browser window restored over a hung Chromium cost two waits on every
frame before, and the desktop's stop wrote it back, so each restart froze
the same way.

Accepting a tab's capture of itself without asking would let any page
take the pixels of whatever it frames, so every tab, from its opening and
in every frame, has a page's own `getDisplayMedia` refused
(`NotAllowedError`); only the module's script holds the real one. A tab
the tap is installed in has every media element muted, so the YouTube
player's own page stays silent at the browser. A tab neither recorded nor
tapped would play on the machine Chromium runs on: the Browser records
every tab it opens.

## What it does not do

- **No JavaScript of its own on the page beyond what it asks.** Pages run
  as they would in Chromium; what is drawn is a snapshot taken when asked,
  so an animation moves only as often as frames are taken.
- **Text is placed, not reflowed.** A page whose text is much larger or
  smaller than a cell keeps its layout and loses spacing; a heading set
  huge is drawn as plain characters where it starts.
- **No file uploads**, and no downloads unless `web downloads` gives them
  a folder. A page's video is the picture its frames are drawn from; its
  sound comes out only through `web sound` or the tap.
- It loads only by name under `--plan`: a browser reaches the network,
  which a plan refuses.
