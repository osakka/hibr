# media — video and sound, the picture drawn as cells

`media` plays video and sound from a file or an address -- anything
FFmpeg's libraries read: MP4, WebM, MKV, MP3, HLS, http and https -- and
draws the picture into the display as coloured half blocks or as ASCII.
The decoding is libavformat and libavcodec, loaded at run time the way
hibr loads libssl and libpng: nothing is linked, no FFmpeg headers are
needed to build, and no ffmpeg or mpv process is started. Sound goes to
the machine's own device -- ALSA on Linux, AudioQueue on macOS -- and the
picture follows the sound's clock.

```text
mod load media                       # or need media
p := media open movie.mp4            # starts playing; -p opens paused
media size $p 80 23 half             # the cells it fills: half, ascii or mono
media frame $p && media draw $p 0 0  # bring the picture up to the clock, draw it
ms := media next $p                  # how long until the next frame is due
media toggle $p; media seek $p +10; media volume $p 60
i := media info $p                   # ${i["pos"]} ["dur"] ["paused"] ["ended"] ...
media close $p
```

`examples/play.hibr` is a whole terminal player in sixty lines:
`hibr examples/play.hibr movie.mp4`.

## Commands

| command | does |
|---|---|
| `media open SRC [-p]` | open a file or address and start playing, or paused; gives the player's number |
| `media play ID`, `media pause ID`, `media toggle ID` | |
| `media seek ID SECONDS [-r]` | go to a time; `-r`, or a leading `+` or `-`, from where it is |
| `media volume ID [0-100]` | set the volume; says what it is |
| `media size ID COLS ROWS [half\|ascii\|mono\|pixels]` | the cells the picture fills, and how it is drawn |
| `media mode ID half\|ascii\|mono\|pixels` | half blocks (two pixels a cell), ASCII by brightness in colour, ASCII plain, or real pixels where the terminal can place them (ADR 0037, which has what a frame costs each way) |
| `media detail ID 0-3` | how finely colours are kept: lower changes fewer cells from frame to frame, which is less to send to the terminal (default 2) |
| `media fps ID N` | the most frames drawn a second (default 24; 0 for every one) |
| `media frame ID` | bring the shown frame up to the clock: status 0 when it changed |
| `media draw ID ROW COL [-p PANE]` | draw the shown frame, into a pane when named |
| `media next ID` | milliseconds until the next frame is due, -1 when nothing will change by itself -- for a caller that sleeps between frames |
| `media info ID` | `pos`, `dur`, `shown` (the time of the frame shown), `paused`, `ended`, `width`, `height`, `volume`, `video`, `audio` (codecs), `output` (where the sound goes), `mode`, `error`; a map with `:=` |
| `media feed [-p]` | a player with no source of its own: its picture and sound arrive on pipes |
| `media pipe ID video\|audio` | a fed player's pipe for one stream, the end to write to as the result; each stream is a container of its own (fragmented MP4, WebM), from its init segment on -- what `web take` writes |
| `media close ID`, `media close all`, `media list` | |

## How it plays

A player is three threads. One reads and decodes, running up to three
seconds ahead; one hands the sound to the device a slice at a time and
keeps the clock; and the shell's own thread, when asked, takes the frame
the clock has reached and draws it. Frames are scaled to the cells while
decoding, with the picture's shape kept and black around it; a frame made
before a resize or a change of mode is resampled when it is drawn, so a
change shows at once and sharpens as new frames arrive.

`HIBR_MEDIA_AUDIO` chooses where sound goes: `alsa`, `audioqueue`, `none`
(silent, at the same pace), or `wav:PATH` (written to a file, at the same
pace -- what the tests listen to). Unset, the platform's own device is
used, and a machine with none plays silently and says so in `info`'s
`error`.

## Fed players

A fed player's pipes are drained into memory by a thread each, so a
writer is never held up by a decoder that has run as far ahead as it may;
a demuxing thread per stream reads that memory through FFmpeg's own I/O
callbacks. A seek drops everything queued and each stream starts again at
the next init segment it is given -- anything before one is taken for a
leftover from before the seek and skipped. The player says it has ended
when its clock reaches the length the init segments declared.

## Which FFmpeg

libavformat 59 to 62 -- FFmpeg 5.1, 6, 7 and 8 -- with libavcodec,
libavutil, libswscale and libswresample of the same release, found on
the system's library path, in `/opt/homebrew/lib`, `/usr/local/lib` or
`/opt/local/lib`, or in `HIBR_MEDIA_LIBDIR`. The newest found is used.
The handful of FFmpeg's structures this reads are declared by where each
field sits in each release, taken from that release's own headers by
`tools/mvoffsets.sh`; a release it does not know is refused by name,
never guessed at. On Debian or Ubuntu the libraries are `libavformat59`
(or later) and friends, already there wherever ffmpeg is; on macOS,
`brew install ffmpeg`.

## What it does not do

- **The terminal sets the frame rate it can bear.** Every changed cell is
  an escape sequence; a 100 by 30 picture at 24 frames is around 400 kB a
  second, fine locally and heavy over a slow link. `fps` and `detail` are
  for that.
- **AudioQueue was written and compiled here but not heard here**: this
  development machine has no sound device. ALSA's path is likewise tested
  only as far as its absence.
- No subtitles, no choosing among several sound tracks (the best is
  taken), no hardware decoding.
- It is refused under `--plan`.
