# salat — prayer times, methods as data

The day's prayer times for a place, by whichever method a person follows
(Gitea #70, ADR 0035): the sun's place computed as Meeus's *Astronomical
Algorithms* gives it, the method's angles and offsets read from a file.

```text
salat list                       name TAB title, one a line
salat times [-m method] [-a shafi|hanafi] [-r middle|seventh|angle]
            [-q general|ahmer|abyad] [-j f,s,d,a,m,i] lat lon [yyyy-mm-dd|time]
salat info [-m method]           name TAB title TAB file
```

`times` gives seven times as epoch seconds -- Fajr, sunrise, Dhuhr, Asr,
Maghrib, Isha, and the middle of the night -- each `-1` where the sun
never reaches the angle (inside the polar circle). It prints, or fills
the slot under `:=`; `printf '%(%H:%M)T'` writes one in local time. The
date is the local calendar day, today when left out.

- `-m` the method, `mwl` unless said otherwise;
- `-a` the Asr school: Shafi'i (a shadow its own length) or Hanafi (twice);
- `-r` the high-latitude rule bounding Fajr and Isha: the middle of the
  night (the default), its last and first seventh, or the method's angles
  over sixty;
- `-q` the twilight the Moonsighting Committee's Isha follows;
- `-j` a person's own minutes for each of the six, after the method's.

## Methods are files

A method is a JSON file in a folder: `$HIBR_SALAT` (colon-separated)
first, then `~/.config/hibr/salat`, then the installed `$(SHAREDIR)/salat`,
the first of a name winning. `mwl` is also built in.

```json
{"name": "mine", "title": "Mine", "fajr": 18, "isha": 17,
 "adjust": [0, 0, 1, 0, 0, 0]}
```

`fajr` and `isha` are the sun's depression in degrees; `isha_minutes`
puts Isha that many minutes after Maghrib instead; `maghrib` an angle for
Maghrib after sunset (Tehran); `adjust` the method's own minutes for
Fajr, sunrise, Dhuhr, Asr, Maghrib and Isha; `rounding` `nearest` (the
default), `up` or `none`; `seasonal` 1 for the Moonsighting Committee's
seasonal Fajr and Isha and its one-seventh rule from 55 degrees.

Bundled: Muslim World League, ISNA, Egypt, Umm al-Qura, Karachi, Dubai,
Moonsighting Committee, Kuwait, Qatar, Singapore, Tehran and Turkey's
Diyanet (approximate), with the parameters adhan-js gives them.

## Checked

`tests/salat_adhan.py` compares every bundled method with adhan-js, an
independent implementation, at sixteen places from the equator to inside
the Arctic circle on every third day of a year, with both Asr schools:
every time to the minute, and a missing time where adhan-js has none. It
skips without node and adhan-js (`npm install --prefix
~/.cache/hibr/adhan adhan@4`). `tests/999-salat.t` records the command.
