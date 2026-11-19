# Asked requests and cross-feature conversions: a design for ADR 0050's next slice

Read-only analysis, 2026-11-19, of `/home/osakka/hibr` (main, with another session's
uncommitted edits in 27 files, 17 of them under `programs/desktop/wm/`) and
`~/hibr-apps` (e1bbcbf). **Line numbers are the working tree's**, so each one has
its function beside it. Where a file exists in both trees, the line is the hibr
tree's copy; the Browser, Video Player and Boxer exist only in `~/hibr-apps`, so
theirs are the catalogue's.

How it was made:
- `python3 tools/appstate.py --back`, with no `--write`.
- A scanner that finds every call and every read in each file the survey names,
  with the function each sits in. It is `back.py` in the scratchpad.
- A recount of cross-feature links with `tests/deps.py`'s own logic: the inspect
  module's `calls` and `refs` records, joined by `# part:` and by catalogue
  bundle. It is `cross2.py` in the scratchpad.
- Every case decided below was then read by hand.

---

## 1. The short version

- **The 36 read-backs are not 36 asks.** Read one by one, they sort into seven
  outcomes:

  | Outcome | Count |
  |---|---|
  | Asked | 8 |
  | Told, the door's ordering enough | 5 |
  | Run by the desktop itself (a menu row naming the request) | 4 |
  | Local in the app's own process | 3 |
  | Write-through of the app's own record | 5 |
  | A new desktop verb | 3 |
  | Not read-backs at all | 8 |

- **Two things the survey cannot see** matter as much as the list:
  - A menu row whose command is a platform request (`dt_item "Clear History" c
    dt_clipclear`) is refused by an app's runtime as an interface function. The
    seam has to run it in the desktop. That is about 73 rows in 40 files,
    `dt_close_focused` 31 of them.
  - Nothing in the app's process redraws when a table arrives today:
    `dt_rtmirrored` only sets `DT_RTDERIVE`.
- **The list is a lower bound.** Five further cases were confirmed by reading
  (§3a):
  - `dt_clipuse` from `cb_use`.
  - `DT_NOTEHIST_ORDER` and `DT_NOTEUNSEEN`, which have no door at all, so the
    Notifications app never sees them move.
  - The Notifications pane's direct clearing.
  - The vault pane's lock, which needs no change.
- **The cross-feature recount gives 36 links.** 28 of them are the ADR's "28
  places" exactly: a caller function and callee with a real call.
  - The other **8** are guard-only links (`dt_has OTHERAPP_fn` before a launch
    or a row). They also go quiet, because `dt_has` is false in a process of
    its own, and they are additions to the ADR's count.
  - Three links (26, 28, 32) only tell another feature that a setting moved.
    Redraw-on-arrival makes them unnecessary.
  - The rest become a library, a routed event, a platform request, or a
    `# part:` line.
- **Something neither input covers:** the platform itself calls installables
  by name in about 20 places (§8). These include `term_passing` on every input
  event, the volume keys into the Sound bar item, and `dt_kbbar` from
  `wm/kbd.hibr`. `tools/callsites.py` only reads dynamic calls, so it never saw
  them.

---

## 2. The outcomes

| Code | Meaning | What changes |
|---|---|---|
| **A** | **Asked.** The app waits for the desktop's answer, and the answer is sent only after every table the request moved. | Only the table: `interface.tsv` row → `asked`, and the stub regenerated. No installable source changes. |
| **T** | **Told, ordering enough.** The read is in a push producer (`_draw`, `_rows`, a bar item's text) and nothing acts on the stale value. The table arrives through its door, and the arrival makes the next push a full one. | Needs **redraw-on-arrival** in the runtime (§5.6). No installable changes. |
| **D** | **Desktop-run.** The call is a menu row's or `dt_mark`'s command naming the platform function. The desktop runs it itself, with `DT_CALLER` set to the owner and the same guards, so no app code runs between the change and the next draw. | A rule in the seam's send-across branch (§5.7). No installable changes. |
| **L** | **Local.** The function only reads files or the environment and fills a table only its caller reads. Asking would put a round trip on a draw path. | **Not table-only.** A local function must exist in the app's own process, which sources only what `interface.hibr` lists. `wm/vault.hibr`, `standby.hibr`, `strip.hibr`, `frame.hibr`, `bar.hibr`, `windows.hibr` and `hold.hibr` are not `# interface: shared`, so each L function moves into a shared file with its helpers and variables. That means `DT_VWCONF`; `DT_STANDBYDIR`, `dt_holdsess`, `dt_sbalive`, `dt_sbboot` and `dt_procstarted`; `dt_barlist` and `dt_swon`; and, for `dt_dlgat`, `dt_dlgfor`, `dt_windisp`, `dt_dispat` and `dt_away`. Then the row flips to `local`, and the table's door is dropped or kept as noted. |
| **W** | **Write-through.** The call writes the caller's own record in a registry no other process writes (its bar item). The stub writes the fields installables read into its own mirror, then sends. | The generator: a small table of which argument fills which field. |
| **V** | **New desktop verb.** The request as written cannot cross. | Installable source changes, and a `hibr:` floor. |
| **X** | **Not a read-back.** It is a comment hit, a top-level declaration, a read before the call, or a field whose door is the call. | Survey fixes (§5.9). |

The rule behind A and T: **an ask is allowed only on an input-paced path**
(`_key`, `_click`, `_do`, `_cut`, `_paste`, a row's setter, a handed callback),
**never in a push producer.** An ask in `_draw` would wake the desktop once per
frame of that app.

Where the read sits in a later *input* handler that acts on the stale value
(the next Delete takes `cb_at`'s id), the answer is **A**, not T. The desktop
may already have sent that next key before it read the app's tell, so the
door's ordering does not cover it. The ask covers it, because the app does not
run the next key until the answer is in.

---

## 3. The 36, one by one

The **Call** and **Read** columns give `file:line [function]`, the hibr tree's
working copy unless marked catalogue.

| # | Installable | Table | Platform call | Call at | Read at | Out | Answer / wrinkle |
|---|---|---|---|---|---|---|---|
| 1 | apps/files.hibr | DT_OPENVERBS | dt_openerverb | none; 2801 is the comment `(dt_openerverb)` | 2840 [files_context] | X | The scanner matched a comment. The real dependency is `dt_openverbs` at 2838, an *answer* request whose answer is `DT_OVLIST`. See §4: its `when` predicates call into Archives. |
| 2 | apps/vault.hibr | DT_VWA | dt_vwacct | 73 [vault_draw], locked branch | 76 [vault_draw] | L | `dt_vwacct` (wm/vault.hibr, the function) only parses `$DT_VWCONF/account.json`, and it runs on every draw of a locked vault. Made local, each process reads the file itself. The door stays for any other reader; the two agree because they read the same file. |
| 3 | apps/vault.hibr | DT_VWJOB | dt_vwsyncnow | 231 [vault_menus], `dt_item "Sync Now" s dt_vwsyncnow 1` | 134 [vault_draw] | D+T | The menu row is run by the desktop. `DT_VWJOB` and `DT_VWSYNCED` arrive through the registry door, and the window redraws on arrival. |
| 4 | control-panel/cliphist.hibr | DT_CHORDER | dt_clipclear | 48 [cliphist_do] `clipclear)` | 35 [cliphist_rows] | T | Only the "N kept" row reads it. The rows are pushed again on arrival. |
| 5 | control-panel/cliphist.hibr | DT_CHORDER | dt_cliptrim | 19 [cp_set_clipmax] | 35 [cliphist_rows] | T | **Wrinkle: order of setting and request.** Line 18 sets `DT_CLIPMAX` in the pane's process, and `cp_save` comes only at 21. A told trim would run with the desktop's *old* maximum. Needs settings-ahead (§5.8). The `dt_clipsave` at 20 is redundant once the trim saves in the desktop. |
| 6 | control-panel/displays.hibr | DT_SBLIST | dt_sbscan | 338 [displays_standby] | 344, 347 [displays_standby] | L | It is a draw path: the pane draws itself. `dt_sbscan` (wm/standby.hibr) reads `$DT_STANDBYDIR` and `HIBR_HOLD`, which the app inherits from `dt_rtspawn`'s exec. The `rm -f` of stale files is harmless run twice. Make it local and give `DT_SBLIST` the class local, or the survey's "door no installable needs" check fails. |
| 7 | control-panel/filetypes.hibr | DT_HANDLERS | dt_handler | 306, 308 [ft_apply] (HEAD 305/307) | 25, 29 [ft_summary]; 39, 45 [filetypes_rows]; 87 [owt_exts]; 155 [ft_dialog] | V+T | `ft_apply` first `unset`s `DT_HANDLERS[$orig]` *directly* (296), and `ft_delete` does the same (194). Neither reaches the desktop. New told requests `dt_handlerset ORIG EXT [term] WORDS...` and `dt_handlerdel EXT` do the unset, the set, the mark and the save in the desktop, which makes the pane's `dt_save` (310, 196) redundant. The rows then follow the door. The working tree carries another session's interim `dt_rtreg DT_HANDLERS` marks at 195 and 297. |
| 8 | control-panel/menubar.hibr | DT_BARI | dt_barilabel | none; 103 is a comment | 119 [cp_baritems] | X | Comment hit. The real calls are `dt_barreading` at 111, an answer request, and `dt_barishown` at 117, a predicate. Both are pure over mirrors, so both become L (§4). |
| 9 | control-panel/menubar.hibr | DT_BARI | dt_bariswitch | none; 105 is a comment | 119 | X | As 8. |
| 10 | control-panel/network.hibr (WebDAV servers) | DRW | dt_davclear | 62 [network_do] `clear)` | 51 [network_rows] | **reclass + T** | `interface.tsv` calls `dt_davclear` **local** (wm/servers.hibr). It reads `DR_LIVE`, edits `DRW` and runs `rm -rf "$DT_DAVCACHE"`, all of it the desktop's. Run in the pane's process it would empty the pane's copy of `DRW` and delete the cache from under the desktop's open remote files. It becomes a told request, and the rows follow the door. |
| 11 | control-panel/shortcuts.hibr | DT_AKEY | dt_keyswap | 30, 31 [shortcuts_do] | 71 [shortcuts_owners] | A | `SC_OWN` feeds the key-capture conflict check, so a stale table can accept a clashing chord. `dt_keyswap` (wm/keys.hibr) **writes three tables through two doors**: `DT_KEYS` and `DT_APPKEY` (save, through its own `dt_save`) and `DT_AKEY` (registry). The ask's flush covers all of them. Answer: done, nothing carried. |
| 12 | control-panel/vaultset.hibr | DT_VWA | dt_vwacct | 96 [vaultset_rows] | 99–133 [vaultset_rows] | L | As 2. `_rows` is a push producer. |
| 13 | control-panel/vaultset.hibr | DT_VWERR | dt_vw | 60 [cp_vwlockjob], 72 [cp_vwpinjob] | 64 [cp_vwlockdone] | V | `dt_vw` runs *inside a job body*. The body is the pane's own function (`cp_vwlockjob`), which `dt_vwjob` hands to `dt_job`/`dt_jobvarsrun`. Told, the desktop's job would run a function the desktop does not have. New verb `dt_vwdo TAG THEN WORDS...`: the desktop's job runs `dt_vw "" WORDS`, `dt_vwjobdone` keeps its generation check, `dt_vwbar` and `DT_VWERR`, then calls THEN through the seam. `DT_VWERR` is marked there, and `dt_rttell` flushes before every tell, so it arrives before THEN. `cp_vwlockjob` and `cp_vwpinjob` go, and `DT_VWST` stays the desktop's. |
| 14 | control-panel/vaultset.hibr | DT_VWERR | dt_vwjob | 56 [cp_set_vwlock], 175 [vaultset_do] `pinoff)` | 64 [cp_vwlockdone] | V | As 13. These are the only installable callers of `dt_vwjob` and `dt_vw`. |
| 15 | control-panel/vaultset.hibr | DT_VWJOB | dt_vwsyncnow | 181 [vaultset_do] | 141 [vaultset_rows] | T | Rows only. `dt_vwsyncnow` returns 0 or the job's status, and nobody reads it. |
| 16 | control-panel/vaultset.hibr | DT_VWSYNCED | dt_vwsyncnow | 181 | 143–144 [vaultset_rows] | T | As 15. |
| 17 | menubar/keyboard.hibr | DT_BARI | dt_barilabel | 42 [dt_kbbar] | 34 [dt_kbbar], before the calls | W | The read at 34 asks whether its *own* item was put on, by an earlier `dt_baritem`. Told, two runs inside one desktop pass would read an old `text` and leave a dead item on the bar. Write-through of `text` and `label` fixes it exactly. |
| 18 | menubar/keyboard.hibr | DT_BARI | dt_bariright | 44 [dt_kbbar] | 34 | W | Writes a command field no installable reads. It is in W only so the bar declarations are handled alike. |
| 19 | menubar/keyboard.hibr | DT_BARI | dt_baritem | 34, 43 [dt_kbbar] | 34 | W | As 17. |
| 20 | menubar/network.hibr | DT_PKRES | dt_pkstart | 758 [dt_netjob], `[ -n "$auth" ] && dt_pkstart && DT_NETJPK=1` | 989 [dt_netjobdone] | A ×2 | **The status is read**, so `dt_pkstart` is asked and its answer is the status. **`DT_PKRES` is read in a job callback that no tell precedes.** `dt_netjob` and `dt_netjobdone` both run in the app's own process, so the desktop's door has nothing to order against. `dt_pkstop` (985 in `dt_netjobdone`, 763 in `dt_netjob`) must be asked too, answered after the flush, so `DT_PKRES` at 989 is the desktop's view at the stop. The agent's pty and the job's end still race, exactly as they do in one process today. **Refcount wrinkle:** `DT_PKUSES` (wm/polkit.hibr) counts across processes, so `dt_rtdrop` must release the uses a dropped connection held. |
| 21 | menubar/tray.hibr | DT_BARI | dt_baridraw | 518, top level | 241 [dt_traybar], 267 [dt_trayat] | X | A declaration at load, read and not run. The reads are other functions, and 267 reads `c`, whose door is the call. |
| 22 | menubar/tray.hibr | DT_BARI | dt_barilabel | 244 [dt_traybar] | 241 [dt_traybar], before | W | As 17. |
| 23 | menubar/tray.hibr | DT_BARI | dt_baripress | 519, top level | 241, 267 | X | As 21. |
| 24 | menubar/tray.hibr | DT_BARI | dt_baristop | 521, top level | 241, 267 | X | As 21. |
| 25 | menubar/tray.hibr | DT_BARI | dt_baritem | 241, 245 [dt_traybar] | 241 | W | As 17. `dt_traybar` runs from the tray's ticker. |
| 26 | menubar/tray.hibr | DT_BARI | dt_bariwheel | 520, top level | 241, 267 | X | As 21. |
| 27 | system/about.hibr | DT_APPS | dt_app | 27, top-level `command -v dt_app && dt_app about ...` | 211–212 [about_apps] | X | A declaration, read at scan time, and `DT_APPS` is sent at the hello. |
| 28 | system/clipboard.hibr | DT_CH | dt_clipclear | 188 [clipboard_menus] row | 67–178 (draw, copy, cut, menus) | D | |
| 29 | system/clipboard.hibr | DT_CH | dt_clipdel | 115 [clipboard_key] `delete)`, then `cb_fit` 118; 159 [clipboard_cut]; 181 [clipboard_menus] row | as 28 | A (115, 159), D (181) | `cb_fit` reads `DT_CHORDER` in the same handler. A held Delete would delete one id twice and skip the next. `clipboard_cut` returns the text with `ret`, after the call, and the desktop's Cut then `dt_clipset`s it. The socket keeps the delete ahead of the cut's answer. |
| 30 | system/clipboard.hibr | DT_CH | dt_clipkeep | 166 [clipboard_paste] | as 28 | A | Input-paced, and the next key reads the order. Nothing carried. |
| 31 | system/clipboard.hibr | DT_CH | dt_clippin | 114 [clipboard_key] `p)`, then `cb_fit` 118; 180 [clipboard_menus] `dt_mark` row | as 28 | A, D | `dt_clippin` writes `DT_CHORDER` only through `dt_cliptrim`, so it is a transitive writer. |
| 32 | system/clipboard.hibr | DT_CHORDER | dt_clipclear | 188 | 31–139 (`cb_at`, `cb_fit`, draw, key, wheel) | D | |
| 33 | system/clipboard.hibr | DT_CHORDER | dt_clipdel | as 29 | as 32 | A, D | |
| 34 | system/clipboard.hibr | DT_CHORDER | dt_clipkeep | 166 | as 32 | A | |
| 35 | system/notifications.hibr | DT_NOTEHIST | dt_notehist_clear | 195, 196 [notifications_menus] rows | 81–273 | D | |
| 36 | system/notifications.hibr | DT_NOTEHIST | dt_notehist_del | 181 [nh_clearsel], then `nh_fit` 182; 219 [notifications_cut], then `nh_fit` 220 | as 35 | A | **`nh_fit` reads `DT_NOTEHIST_ORDER`, which has no door at all** (§3a), so even an ask does not fix this until that table is given one. |

Tally:
- X: 1, 8, 9, 21, 23, 24, 26, 27.
- L: 2, 6, 12.
- W: 17, 18, 19, 22, 25.
- D only: 28, 32, 35. Item 3 is D+T.
- T: 4, 5, 15, 16, and 10 after its reclass.
- A: 11, 20, 29, 30, 31, 33, 34, 36. Items 29, 31 and 33 also have D rows.
- V: 7, 13, 14.

**Only the V items need installable source changes**: `control-panel/filetypes.hibr`
and `control-panel/vaultset.hibr`. The A, D, T and W items are table, runtime or
generator work. **The L items also move function bodies** out of `wm/` files
that are not shared, into shared ones (§2), so they touch the platform's
source, though no installable's.

`interface.tsv`'s `since` column is `-` on every row, and `tests/appcalls.py`
does not yet compare against the last tag. So a row's class can flip freely
now; it cannot once the first release with the contract ships.

### 3a. Missed by the survey, confirmed by reading the bodies

| Where | What | Out |
|---|---|---|
| system/clipboard.hibr 113 `enter) cb_use` → 127 [cb_use] `dt_clipuse` | `dt_clipuse` (wm/clip.hibr) writes `DT_CH` and `DT_CHORDER` only through `dt_clipdel` and `dt_clipset`→`dt_clipkeep`, so the survey's direct-writer rule misses it. | A |
| `DT_NOTEHIST_ORDER` | It is read in system/notifications.hibr: `nh_fit` 42, `nh_at` 53, draw 65, key 128, menus 192. It is also read in control-panel/notifications.hibr at 114 and 146. The survey classes it `request`, door `-`, because an installable writes it, so **it is never sent**. `dt_notehist_del` and `dt_notehist_clear` write it and mark only `DT_NOTEHIST`. | Door **registry**, and every writer in wm/notify.hibr marks it, including the add path in `dt_notify`. |
| `DT_NOTEUNSEEN` | It is written directly in system/notifications.hibr:64 [notifications_draw] and control-panel/notifications.hibr:150 [cp_clearhist]. The desktop's bell reads it, and written in an app's process the bell never clears. | A new told request `dt_noteseen`. A write in a draw is the wrong place for it anyway; it belongs on `_focus`. |
| control-panel/notifications.hibr 140–151 [cp_clearhist] | It empties `DT_NOTEHIST` and `DT_NOTEHIST_ORDER` by hand. The working tree carries another session's interim mark at 145. | Replace with `dt_notehist_clear urgent` (T). |
| control-panel/vaultset.hibr 166 `lock) dt_vwlock` | Reaches `DT_VWA`, `DT_VWERR` and `VW_SESSION` through `dt_vwjob` and `dt_vwforget`. Rows only. | T, no change |

The transitive walk I ran across the call graph was mostly noise (`dt_has`,
`dt_menu`), so these five are only the ones read and confirmed. Fixing the survey
(§5.9) is how the rest get found.

---

## 4. Related: requests that answer through a global (the "answer" door)

`DT_OVLIST`, `DT_OWLIST`, `CS_MOD_ORD`, `DT_BTNL`, `DT_BARREAD` and `DT_DLGR/C/H/W`
are the answers of `dt_openverbs`, `dt_openers`, `cs_ordered`, `dt_btnlist`,
`dt_barreading` and `dt_dlgat`. `interface.tsv` calls all of them **told**, and
three of them `declare`. Told, the caller reads an empty or stale list.
`dt_rtmirrored` also refuses the `answer` door today.

Each one, read:

- **`cs_ordered`, `dt_btnlist`, `dt_barreading`, `dt_barishown`: pure over mirrors**
  (`CS_ORDER`/`CS_MOD_ALL`, `DT_BTNORDER`/`DT_BTNS`, `DT_BARI` and the bar's
  layout). → **L**. No asks.
- **`dt_dlgat`** (55 calls in 38 files) works through `dt_dlgfor`. It reads
  `DT_FOCUS` (hot), the focused window's record, and the display layout.
  - The window door sends an app only its own windows. A dialog opened while
    another app's window has focus therefore falls back to the primary display,
    which is a small difference.
  - **L** if the layout (`DTW_*` through `dt_dispat`) is mirrored; otherwise
    **A** carrying `DT_DLGR/C/H/W`. L is far cheaper at 55 call sites.
- **`dt_openers`**: **A**, carrying `DT_OWLIST`.
- **`dt_openverbs`** (wm/handlers.hibr):
  - **A**, carrying `DT_OVLIST`, **but its body calls into other installables**.
    - It runs `dt_appfn owner WHEN path` for each verb with a `when`: Archives'
      `arc_canextract` and `arc_cancompress`, registered at
      apps/archives.hibr 862–864.
    - It also runs `dt_has "$call"` and `dt_has "${ap}_open"`, which are false
      in the desktop once apps are hosted.
  - A desktop answering an ask cannot wait on a third process, nor on the asker
    itself, which is blocked waiting for the answer. So `when` has to stop
    being a callback. That decision is the owner's (§10).

---

## 5. The mechanism for asked requests

It is built from what exists: the wire in `wm/rtwire.hibr`, `dt_rtmsg`,
`dt_rttell` and `dt_rtsay` in `wm/rtserve.hibr`, `dt_rtsend`, `dt_rtcalled` and
`dt_rtrun` in `runtime.hibr`, and the stub generator in `tools/appcalls.py --stubs`.

### 5.1 Message shape (protocol 5)

| Direction | Line | Meaning |
|---|---|---|
| app → desktop | `ask SEQ NAME WORDS...` | `SEQ` is a counter per process, starting at 1. |
| desktop → app | *door lines* | The ordinary `set`/`json`/`jsonk`/`unsetk`/`win`/`wingone` lines that the flush produces. |
| desktop → app | `set NAME V` / `json NAME TEXT` | For each **answer-door** name the request fills. Accepted by the runtime only while `SEQ` is awaited, and only for names in that request's new `gives` column. |
| desktop → app | `answer SEQ STATUS KIND VALUE` | `KIND` is `s` (a scalar `VALUE`), `j` (`VALUE` is JSON for a map) or `-` (nothing given with `ret`). |

`DT_RTPROTO` 4 → 5. A process speaking 4 is refused at its hello, as today.

### 5.2 Desktop side (`dt_rtmsg`, a new `ask)` arm)

1. **The same three guards as `tell`.**
   - The name must be in `DT_RTASK`, which is filled from `interface.tsv`'s
     `ask` rows the way `DT_RTALLOW` is from the told rows.
   - The name must be in `DT_RTMAY["$who NAME"]`.
   - The first word must name an owned window where `DT_RTWIN` says so.
   - **A refusal is answered** (`answer SEQ 126 - `) and logged, never left to
     the app's timeout.
2. **Run it as the seam does:**
   `r := dt_appfn "$who" NAME WORDS...; st=$?`, timed with
   `dt_spent NAME ... "$who"` as tells are. `dt_appfn` ends in `&& ret -k`, so
   a successful callee's `ret` comes through without the "never calls ret"
   warning. A failing callee answers its status with nothing given, which is
   the seam's behaviour today.
3. **Send ahead: one helper shared with `dt_rttell`.**
   - Factor `dt_rthot` plus `dt_rtwinsc "$c"` out of `dt_rttell` into
     `dt_rtahead c`.
   - `dt_rthot` already flushes every marked registry (`dt_rtregflush`) and
     sends moved hot scalars.
   - The settings door is already synchronous inside `dt_save`.
   - Every table the request moved is then queued to this connection ahead of
     the answer.
4. **Then any answer-door names, then the answer, all through `dt_rtsay`.**
   `send -p` keeps what the app has not taken *in order*, so a backlog never
   reorders the answer ahead of the tables.

Two rules go with it:

- **An `ask` request's desktop body may not read the answer of any seam call**
  into an installable: no value, no status. It may *tell*.
  - `dt_openverbs` breaks this rule today.
  - Check it in `tools/seamcalls.py`. Walk each `ask` request's platform call
    graph and fail on a seam call whose `use` is `value` or `status`.
- **Requests that run programs synchronously** (`dt_vw` is the example) must
  never be desktop-run asks. They stall the desktop, and every app with it.
  The V conversion of `dt_vwdo` puts that work in a job.

### 5.3 App side (`runtime.hibr`)

**This mechanism needs a marker of its own.** `interface.tsv` already calls 16
requests `asked`, because their answer is read. The ADR resolves those without a
round trip:
- `dt_new` takes an id from a block handed out at the hello.
- `dt_ids`, `dt_title`, `dt_locof`, `dt_vwstate`, `dt_vwuntil` and the other
  predicates become pushed facts.
- `dt_launch`, `dt_place` and the rest answer by callback.

Some of those sit in push producers: `dt_vwstate` in `vault_draw` and
`vaultset_rows`. A blanket "every `asked` row becomes a round trip" would put
asks on draw paths on day one, and fail the push-producer check below.

So the round-trip kind gets its own value in the `answer` column, **`ask`**:
- It serves the read-backs here: `dt_clipdel/pin/keep/use`,
  `dt_notehist_del`, `dt_keyswap`, `dt_pkstart` and `dt_pkstop`.
- It serves the answer-door requests that stay requests: `dt_openers`,
  `dt_openverbs`, and `dt_dlgat` until it can be local.
- The ADR's 31 keep their planned resolutions. Its "actions whose failure the
  app checks" (`dt_save`, `dt_shot`, `dt_setgeom` and the rest) could use
  `ask` where the caller is input-paced, which is the owner's call.

The stubs are generated. For `ask` rows, `requests.hibr` becomes
`dt_rtask NAME "$@"` in place of today's `dt_rtsend ask ...`, which only logs
"not carried yet". `dt_rtask`:

1. **Sends settings-ahead (§5.8), then `ask SEQ ...`.**
2. **Waits on the socket alone** until the answer or `DT_RTASKMS`.
   - `console key` cannot be the wait: it also wakes on whatever the app
     itself watches, and would spin on a busy fifo until the deadline.
   - What is needed is a bounded wait on one descriptor. Either `console key -o FD`
     (the console module, no core change), or `recv -p -t MS` in `src/net.c`.
   - `read -t` would also bound it, but on a socket it reads a byte per
     syscall. That is too slow for the clipboard's history arriving in the
     same flush.
3. **While waiting:**
   - **Apply mirror lines in arrival order, always**:
     `set/json/jsonk/unsetk/win/wingone`.
   - **Queue `tell` lines** in a process-wide `DT_RTQ` and never run them now.
     The app is single-threaded and the caller is mid-callback. No re-entry.
   - **Lines the outer loop had already taken come first.** `dt_rtrun` copies
     `DT_RTGOT` into a local `got` before running anything, so a line that
     preceded the ask but had not yet been applied would otherwise be applied
     *after* the newer flush, regressing a table.
     - So `dt_rtrun` keeps its batch in `DT_RTQ` and pops one line at a time.
     - The ask drains `DT_RTQ` first: mirrors applied, tells kept, in order.
     - It honours `DT_RTMORE` the same way the hello loop does.
   - The invariant: **state only ever moves forward.** A deferred tell may see
     newer state than when it was sent, which is fine. An older mirror line
     may never be applied after a newer one.
4. **On `answer SEQ`:**
   - Run `dt_rtderived`, so what the settings derive is rebuilt before the
     caller reads anything.
   - Set the status, hand `ret` back (a scalar with `ret`, a map the way
     `dt_appfn` does with `ret -k`), and return.
   - `dt_rtrun` then runs the queued tells before reading the socket again.
5. **Answers for any other SEQ** are dropped and logged: they are late answers
   to an ask that timed out.

### 5.4 Timeouts, and what an unanswered ask means

- **The desktop never waits on an app**, so an unanswered ask means one of two
  things. Either the desktop itself is stalled, which is already a `held: pass`
  line, or the desktop has gone, which ends the socket.
- **`DT_RTASKMS`** defaults to 2000, the hello's own bound. Below a stalled
  pass it would only add noise.
- **On timeout, the ask returns status 125.** The log says
  `app runtime: NAME asked and not answered in N ms`. The app goes on with the
  tables it has.
  - The request still runs when the desktop reaches it. Its tables arrive
    later through the doors, and redraw-on-arrival repairs the view.
  - The late answer is discarded by SEQ.
- **126 is a refusal; 1 is the desktop gone.** `dt_rtrun` then notices the end
  of the socket as now.
- **Blame:** a wait on the desktop is *not* the app's `held:`.
  - The app's budget instruments time its callbacks. An ask's wait must be
    excluded from them, or named apart as `asked: NAME N ms`, so a slow desktop
    request is never charged to the app.
  - The desktop's own `dt_spent NAME ... who` already names the request and
    the app that asked.
  - The sensitivity requirement gains one per-app number: ask round-trip time.

### 5.5 What an ask costs

- The desktop: a request run, which happens anyway, plus one line. It never
  waits.
- The app: idle, it is a wake of the desktop through `console woke` and one
  `dt_rttick`, typically well under a millisecond. Busy, it is at most one
  desktop pass.
- Every A item above is input-paced: one round trip per menu pick or key, in
  the app only.

### 5.6 Redraw on arrival (needed by every T, D and L-with-door item)

- `dt_rtcalled`/`dt_rtmirrored` set `DT_RTDERIVE` when a line arrives, and
  `dt_rtderived` only re-runs `dt_derive`. Add `DT_RTSTALE`: set whenever a
  batch changed any door name (or `win`/`wingone`), and cleared by the push
  loop. While it is set, the push loop treats every window, pane rows and bar
  text of this process as dirty. The cost lands in the app only.
- Until hosted cells exist, the flag has no consumer. **Build it now and check
  it in `tests/rtchan.py`**: a window's `_dirty` answers dirty after a
  registry line, and clean after a pass with none.
- This also retires several cross-feature calls: `dt_kbbar`,
  `term_cursorapply` and `dt_pnoteset` (§6).

### 5.7 Desktop-run handed commands (D)

- `dt_mdo` (wm/menus.hibr 1741) calls `dt_appfn OWNER CMD...` for a row's
  command, and `seamcalls.tsv` classes it `handed`/`ask`. When `CMD` is a
  platform request (it is in `DT_RTALLOW` or `DT_RTASK`) and the owner may make
  it (`DT_RTMAY`), the seam's send-across branch runs it in the desktop with
  `DT_CALLER=OWNER`. It never tells the app, because the runtime refuses an
  interface name as "not this app's own".
- Counted with a rough regex over `dt_item`/`dt_mark`/`dt_radio` rows: about
  **73 rows in 40 files**.
  - `dt_close_focused` 31, `dt_launch` 13, `dt_panelpane` 9, `dt_edit` 6.
  - Then the clipboard, notification and vault rows above.
- The same holds for a pane row's setter or command naming a platform function.

### 5.8 Settings ahead of requests (app → desktop)

- The mirror image of `dt_rttell` running `dt_rthot` first. Before `dt_rtsend`
  or `dt_rtask` sends anything, the runtime compares the kept settings this
  process may write with what it last sent or received. Its allowlist is the
  `DT_KEEP` names its own files assign. It sends each one that moved, as the
  settings request the kept-settings slice will accept.
- Needed by item 5 (`DT_CLIPMAX` then `dt_cliptrim`). The same shape appears
  in every pane setter that sets a variable and then calls a request.
- It depends on the kept-settings slice's acceptance path, which is not built.
  The interim alternative, passing the maximum to `dt_cliptrim`, is a
  signature change and an installable source change to `cliphist.hibr`. That is
  free while `since` is `-`, but it is not table-only.

### 5.9 How the survey and tests hold it

- **`tools/appstate.py --back`:**
  - Strip comments before matching calls. That removes 1, 8 and 9.
  - Skip top-level declarations. That removes 21, 23, 24, 26 and 27.
  - Skip reads that come before the call in the same function, or read a field
    whose door is `call`.
  - Follow writers through platform functions, as `appcalls.py`'s `reaches`
    already does. That finds `dt_clipuse`.
  - Then **fail** a read-back whose call is told, unless its row in a new
    reasoned table says T, D, L or W.
  - Fail a table that installables both read and write with door `-`.
    That catches `DT_NOTEHIST_ORDER`.
- **`tools/appcalls.py`:**
  - The `answer` column gains `readback` and `gives` (the answer-door globals
    a request fills).
  - The generator emits `dt_rtask` stubs, and the write-through table for W
    (`dt_baritem` → `DT_BARI[$1]["text"]=$2`, `dt_barilabel` →
    `DT_BARI[$1]["label"]=$2`).
  - Fail an `ask` request reachable from a push kind (`_draw`, `_rows`,
    `_dirty`, a bar item's text or draw, a strip draw, a saver frame).
- **`tools/seamcalls.py`:** the rule in §5.2, that an `ask` body reads no seam
  answer.
- **`tests/rtchan.py`.** Each check below must fail with that half taken out:
  - an ask answered with a status, a scalar `ret` and a map `ret`;
  - a request that marks a registry the asker reads, read on the next line
    after the ask, sees the new table — and the check fails with the row
    downgraded to told;
  - a refused ask answered 126 at once;
  - a desktop stand-in that never answers gives 125 within `DT_RTASKMS`, the
    app's loop goes on, and a late answer is dropped;
  - tells arriving during an ask run after it, in order, never re-entrant;
  - a mirror line already taken by the outer loop is applied before the ask's
    flush, never after (the `DT_RTMORE` case);
  - redraw-on-arrival;
  - a desktop-run handed command;
  - `dt_rtdrop` releases polkit uses.

---

## 6. The cross-feature links (36), and what each becomes

The recount uses `deps.py`'s logic: inspect's `calls` plus `refs` with the owner
symbols, units joined by `# part:` and by catalogue bundle. It gives 63 facts.

- Thirteen are hits on Sheet's formula names (`min`, `max`, `sum`, `avg`,
  `count`) used as plain words in dbase, boxerplay, files `fb_clamp`,
  switchboard, wiretap and the blit pane. They are false and are dropped.
- That leaves **51 facts, 36 links (caller function × callee), in 33 functions,
  across 19 pairs of files**.
- **The ADR's 28 places are reproduced exactly.** They are the links with a
  real call: a regex count of real calls also gives 28 (file, function,
  callee) places, with 30 calls. The ADR's 35 calls include guard references
  counted differently.
- **The other 8 links are guard-only, and are additions to the ADR's count:**
  links 1, 5, 11, 20, 24, 25, 27 and 35. They go quiet exactly as a call does.

Kinds:

| Kind | What it becomes |
|---|---|
| **FACT** | A presence guard: `dt_has OTHERAPP_fn`, false in a process of its own. Becomes a predicate over a mirrored registry. **One new piece:** a registry of the installables loaded (`DT_LOADED`, kind-relative path → kind; registry door; marked by the loaders and by `dt_rescan`), with `dt_installed REL` local over it. `DT_APPS` answers for apps; it cannot answer for a bar item or a pane. |
| **REACT** | A call that only tells another feature a setting moved. Dropped. The owner reacts when the setting arrives (§5.6), by comparing in its own draw what it last applied. |
| **LIB** | Shared code moves into a library both source, behind the `*_LIBDONE` guard, loaded into the zygote (ADR 0050). A store both write is protected by the existing lock folders, and each process watches the store with `dirwatch` (ADR 0050, 6a). |
| **EVENT** | A platform request the desktop fans out as a tell to every process that registered a hook for it. |
| **PLAT** | The work is the desktop's own (settings, display state), so it becomes a platform request. |

| # | Caller `file:line [fn]` | Callee (file) | Kind | Becomes |
|---|---|---|---|---|
| 1 | apps/Games/boxer.hibr:1415 [bx_searchweb] (catalogue) | browser_draw (Browser) | FACT | `dt_appknown browser && dt_launch browser "$u"` |
| 2 | apps/Internet/browser.hibr:3975–3976 [bw_cover] (catalogue) | bx_gamelist (Boxer) | LIB | `lib/boxes.hibr`: the shelf scan over Boxer's `BX_SHELF`, which the Browser's process must be sent. Coordinate with hibr-browser. |
| 3 | browser.hibr:3980 [bw_cover] | bx_games (Boxer) | LIB | Goes; it was the pre-1.29 fallback. Raise the `boxer` floor instead. |
| 4 | browser.hibr:4055 [bw_coverdone] | bx_setcover (Boxer) | LIB | `lib/boxes.hibr` gets `bm_up`/`bc_set`'s cover write. Boxer's window notices the cover file change. **The largest extraction in this list.** |
| 5 | browser.hibr:4136 [browser_context] | bx_setcover guard | FACT | `dt_appknown boxer` |
| 6 | apps/Internet/mail.hibr:1039, 1042 [ml_read] | cv_card (Calendar) | LIB | Into `lib/pim.hibr` (or a new `lib/pimcard.hibr`). The card is drawn in Mail's process from the `.ics`. Guard → `dt_appknown calendar`, or none. |
| 7 | mail.hibr:1232 [ml_drawrsvp] | cv_actlabel (Calendar) | LIB | Same library. It is a label table. |
| 8 | mail.hibr:1247–1248 [ml_rsvp] | cv_act (Calendar) | LIB | `cv_act` and `cv_dest` into `lib/pim.hibr`, writing Calendar's store in Mail's process under the sync lock. Calendar's process sees the store change by `dirwatch`. **Its status is read** (`|| return 0`, then `ml_read`), so routing it to Calendar's process would need a callback; the library keeps it synchronous. |
| 9 | mail.hibr:1558–1559 [ml_addsender] | pe_add (Contacts) | LIB | `pe_add`, with `pe_put` moved into `lib/pim.hibr`. Contacts watches its store. Guard → `dt_appknown contacts`. |
| 10 | mail.hibr:1583–1584 [mlc_suggest] | pe_suggest (Contacts) | LIB | Its contacts-store half into `lib/pim.hibr`. Its Mail half (`ml_people`) is Mail's own in Mail's process. |
| 11 | apps/Office/pdfr.hibr:671 [pr_follow] | browser_open guard | FACT | `dt_appknown browser` |
| 12 | apps/Sound & Video/video.hibr:199 [vp_has] (catalogue) | bw_cfg webkit (Browser) | own code | The Video Player has its own `web` module in its own process, so it says `web config engine wpe` itself. The Browser's other `web config` lines (program, args, error page, webrtc, gpu) are the Browser's. ADR 6 already gives Video its own profile. Coordinate with hibr-browser. |
| 13 | video.hibr:233 [vp_tab] | bw_cfg | own code | As 12. |
| 14 | apps/archives.hibr:381, 386 [arc_indexed] | fb_relist (Files) | EVENT | `dt_pathchanged PATH [BACK]`, told. The desktop tells every process that registered `dt_onpath FN`, and Files registers `fb_relist`. `dirwatch` already covers real folders since 0.99.520; this is needed for `archive://`. |
| 15 | archives.hibr:764 [arc_extracted] | fb_relist | EVENT | As 14. |
| 16 | archives.hibr:854 [arc_compressed] | fb_relist | EVENT | As 14. |
| 17 | apps/calendar.hibr:1243 [cv_acct] | ml_accounts (Mail) | LIB | `ml_accounts` is only `email accounts` plus maps, so it moves to a small `lib/mailacct.hibr` both source. `cv_act` reads `MLME`, which then comes from the library in either process. |
| 18 | apps/contacts.hibr:151–152 [pe_suggest] | ml_people (Mail) | LIB | Needs Mail's message index (`MLN`/`ML` through `ml_load`) in Contacts' process. Better: Mail keeps a "people" file in its store, and the library reads that. |
| 19 | contacts.hibr:503–504 [pe_mail] | mlc_to (Mail) | PLAT, **unverified** | `mailc` has no `_open` (mail.hibr 29, 1721), so `dt_launch mailc ADDR` does not work. `dt_openlink "mailto:ADDR"` reaches `mlc_new` today only by a literal call (`wm/apps.hibr:776–777`), which itself goes quiet (§8). Needs either a `mailc_open` that takes an address, or Mail registering a `mailto:` opener that `dt_openlink` routes through the seam. Guard → `dt_appknown mail`. |
| 20 | apps/files.hibr:2862 [files_context] | term_draw guard (Terminal) | FACT | `dt_appknown term` |
| 21 | control-panel/desktop.hibr:152 [desktop_rows] | cp_set_shotfmt, handed to `cp_choice` (control-panel/screenshots.hibr) | part | `screenshots.hibr` is not a pane of its own. It has no `app` line, and its header says it holds "the choices for the Desktop pane's screenshot rows": `SH_FMTS` and `SH_STARTS`, which `desktop_rows` also reads (152, 156), plus the two setters and the folder dialog. It is the Desktop pane's helper that never said so. **Add `# part: control-panel/screenshots.hibr` to `control-panel/desktop.hibr`**, and links 21–23, and the two arrays, are inside one process. The `dt_has` guards can then go. Shipped only, no catalogue twin. |
| 22 | desktop.hibr:156 [desktop_rows] | cp_set_shotlast | part | As 21. |
| 23 | desktop.hibr:177 [desktop_do] | shd_dialog | part | As 21. |
| 24 | desktop.hibr:180 [desktop_do] | files_draw guard | FACT | `dt_appknown files` |
| 25 | control-panel/language.hibr:201, 210 [language_rows] | dt_kbbar guard (keyboard bar item) | FACT | `dt_installed menubar/keyboard.hibr` |
| 26 | language.hibr:265, 267 [language_do] | dt_kbbar | REACT | The pane sets `DT_KBNAME`. The bar item's process re-renders its text when the setting arrives (§5.6). The call goes. |
| 27 | control-panel/notifications.hibr:204 [notify_rows] | dt_pnoteset guard (pnotes pane) | FACT | `dt_installed control-panel/pnotes.hibr` |
| 28 | notifications.hibr:228–229 [notify_do] | dt_pnoteset | REACT | The pane toggles `DT_PNOTES`. pnotes' process runs its `dbus notes on/off` when the setting arrives. The `dbus` call has to live where the bus connection is. |
| 29 | control-panel/wallpaper.hibr:46–47 [wp_cur] | cp_wdget (wallset pane) | PLAT + owner | Per-display wallpaper (`DT_WALLDIMG/DMODE/DPIC`, two of which the survey already lists as requests) becomes platform: `dt_walldget` local over the mirrors, and `dt_walldset DISP FIELD VALUE` told. **But `CP_WDISP` — the display wallset has selected — drives the Wallpaper pane's dialog. That is UI state across two processes.** See §10. |
| 30 | wallpaper.hibr:58–59 [wp_put] | cp_wdset | PLAT + owner | As 29. |
| 31 | control-panel/wallset.hibr:198 [wallset_do] | wp_dialog (Wallpaper pane) | owner | As 29: parts of one process, or an EVENT. |
| 32 | control-strip/cursor.hibr:32 [cursor_pick] | term_cursorapply (Terminal) | REACT | `term_draw` (or `term_dirty`) applies `DT_CURSOR` when it differs from what the window last applied. The call goes. |
| 33 | control-strip/power.hibr:28 [pwr_draw] | dt_pwicon (Power bar item) | LIB | `lib/power.hibr` holds the readings (`DT_PW`, local per process), `dt_pwicon` and the menu. The strip reads the battery itself, so there are **two pollers**. See §10. |
| 34 | power.hibr:41–42 [pwr_context] | pw_context | LIB | As 33. |
| 35 | control-strip/theme.hibr:22 [theme_click] | cp_theme guard (Appearance) | PLAT | **The strip also reads Appearance's own `CP_THEME` and `CP_THEMES`, which is state across features and not counted in the 36.** Theme names become a registry (themes are data). |
| 36 | theme.hibr:29–30 [theme_pick] | cp_theme (Appearance) | PLAT + owner | `cp_theme` (appearance.hibr 282) sets about 25 desktop settings from a theme file. As `dt_themeset NAME` in the platform, the pane and the strip both call it, and the desktop accepts the settings because the request is its own. **That moves Appearance's code into the package.** See §10. |

Links 6–10 and 17–19 are the same feature cluster (PIM and Mail). Links 2–5,
12 and 13 are hibr-browser territory; the Browser and Video Player live in
`~/hibr-apps` and are never `addapp`ed from this tree.

---

## 7. Batches: no two batches edit the same file

**Batch 0 goes first** and owns every shared file. The others can run in
parallel once Batch 0 has merged its API:
- the names of the new helpers;
- the new requests' signatures;
- **the names of the new shared files the L moves go into**, already listed in
  `interface.hibr`'s loop.

Each other batch fills its own new shared file, so `interface.hibr` is
edited once.

**Another session's uncommitted work.** It already touches most of Batch 0's
files: `runtime.hibr`, `wm/rtserve.hibr`, `wm/rtwire.hibr`, `mirrors.hibr`,
`tools/appstate.py`, `tests/rtchan.py`, `tests/screen.py` and the ADR. It also
touches several batches' `wm/` files: `apps`, `bar`, `clip`, `handlers`,
`jobs`, `kbd`, `keys`, `notify`, `polkit`, `remote`, `servers`, `settings`,
`standby`, `strip` and `vault`. Settle with that session before any batch
starts, and stage by name.

Each batch writes its checks into **a new suite of its own**. Check each new
suite's name against the standard library: `python3 -c 'import NAME'` from
outside `tests/`. `tests/apps.py`, `tests/appslice.py` and `tests/rtchan.py`
plan counts would otherwise collide.

**Catalogue twins:** every tree file here that has a twin in `~/hibr-apps` needs
its bump and `addapp`. Run `./stale` first, check `git status` before
`mkindex`, and raise the `hibr:` floor where an installable's source changed.

| Batch | Scope | Files (exclusive) |
|---|---|---|
| **0 — mechanism and table** (one person) | Protocol 5 `ask`/`answer`; `dt_rtahead`; `DT_RTQ`; `DT_RTSTALE`; settings-ahead; desktop-run handed commands in the seam; W write-through; the `ask` marker and the `gives` column. The row flips: `ask` for `dt_clipdel/pin/keep/use`, `dt_notehist_del`, `dt_keyswap`, `dt_pkstart`, `dt_pkstop`, `dt_openers`, `dt_openverbs` and `dt_dlgat`; `dt_davclear` local → request. The L flips land with each batch's body move. Survey fixes; doors for `DT_NOTEHIST_ORDER` and `DT_NOTEUNSEEN`; `DT_LOADED`, `dt_installed` and `dt_appknown`. Also the §8 sites in `wm/apps.hibr` — 610–611 (Appearance's lists, with Batch 9's API), 776–777 (`mailto:`, with Batch 12's answer), 859 and 878–879 — since it owns that file. | `programs/desktop/runtime.hibr`, `requests.hibr` (generated), `interface.tsv`, `interface.hibr`, `mirrors.hibr` (generated), `seamcalls.tsv`, `callsites.tsv`, `wm/rtserve.hibr`, `wm/rtwire.hibr`, `wm/seam.hibr`, `wm/apps.hibr`, `tools/appcalls.py`, `tools/appstate.py`, `tools/seamcalls.py`, `tools/callsites.py`, `docs/adr/0050-apps-in-processes-of-their-own.md`, `docs/adr/0050-state.tsv`, `tests/rtchan.py`, `tests/appcalls.py`, `tests/appseam.py`, `tests/screen.py` |
| **1 — clipboard** | Table-only (A, D, T): checks only. Item 5 waits for settings-ahead, or takes the interim signature change in `cliphist.hibr`. | `wm/clip.hibr`, `system/clipboard.hibr`, `control-panel/cliphist.hibr` |
| **2 — notification history** | Mark `DT_NOTEHIST_ORDER` and `DT_NOTEUNSEEN` in every writer; `dt_noteseen`; `cp_clearhist` → `dt_notehist_clear urgent`; links 27–28; the §8 sites `wm/notify.hibr:545, 801, 811`. | `wm/notify.hibr`, `system/notifications.hibr`, `control-panel/notifications.hibr`, `control-panel/pnotes.hibr` |
| **3 — vault** | `dt_vwdo`; `vaultset` off `dt_vwjob`/`dt_vw`; L: `dt_vwacct` and `DT_VWCONF` into a new shared `wm/vwacct.hibr`. | `wm/vault.hibr`, `wm/vwacct.hibr` (new), `control-panel/vaultset.hibr`, `apps/vault.hibr` (checks only) |
| **4 — handlers, Files, Archives** | `dt_handlerset`/`dt_handlerdel`; File Types onto them; `dt_openverbs`' `when` (owner's answer); `dt_pathchanged`/`dt_onpath`; links 14–16 and 20; the §8 sites `wm/handlers.hibr:300` and `wm/shot.hibr:278–280`. | `wm/handlers.hibr`, `wm/files.hibr`, `wm/shot.hibr`, `control-panel/filetypes.hibr`, `apps/files.hibr`, `apps/archives.hibr` |
| **5 — bar items and keyboard** | Checks for W; links 25–26; L: `dt_barreading`, `dt_barlist`, `dt_barishown` and `dt_swon` into a new shared `wm/barfacts.hibr`. | `wm/bar.hibr`, `wm/barfacts.hibr` (new), `menubar/keyboard.hibr`, `menubar/tray.hibr`, `control-panel/language.hibr`, `control-panel/menubar.hibr` |
| **6 — polkit, WebDAV, standby** | The per-owner use count released on drop (the `dt_rtdrop` hook is Batch 0's); `dt_davclear` checks; L: `dt_sbscan` with `dt_sbalive`, `dt_sbboot`, `dt_procstarted`, `DT_STANDBYDIR` and `dt_holdsess` into a new shared `wm/sbscan.hibr`, with `dt_holdsess` taken out of `wm/hold.hibr`. | `wm/polkit.hibr`, `wm/servers.hibr`, `wm/standby.hibr`, `wm/hold.hibr`, `wm/sbscan.hibr` (new), `menubar/network.hibr`, `control-panel/network.hibr`, `control-panel/displays.hibr` |
| **7 — Desktop pane and Screenshots** | Links 21–23: the `# part:` line and dropping the guards; link 24 → `dt_appknown files`. | `control-panel/desktop.hibr`, `control-panel/screenshots.hibr` |
| **8 — wallpaper** | Links 29–31, after the owner's answer. | `wm/wallpaper.hibr`, `control-panel/wallpaper.hibr`, `control-panel/wallset.hibr` |
| **9 — theme** | Link 36's `dt_themeset`, and link 35. | `wm/settings.hibr` (or a new `wm/theme.hibr`), `control-panel/appearance.hibr`, `control-strip/theme.hibr` |
| **10 — terminal and cursor** | Link 32 (REACT); the Terminal pushes its pass-through state as a fact on its window record, for Batch 14 to read. | `apps/term.hibr`, `control-strip/cursor.hibr` |
| **11 — power** | `lib/power.hibr`; links 33–34. | `menubar/power.hibr`, `control-strip/power.hibr`, `lib/power.hibr` (new) |
| **12 — PIM and Mail** | Links 6–10 and 17–19; `mailc`'s address entry for link 19. | `apps/Internet/mail.hibr`, `apps/calendar.hibr`, `apps/contacts.hibr`, `lib/pim.hibr`, `lib/mailacct.hibr` (new), `control-panel/pimset.hibr` if it is touched |
| **13 — hibr-browser territory** (catalogue only) | Links 1–5 and 11–13. | `~/hibr-apps/bundles/apps/Internet/browser.hibr`, `.../apps/Games/boxer.hibr`, `.../apps/Sound & Video/video.hibr`, `apps/Office/pdfr.hibr` (tree and twin), `lib/boxes.hibr` (new) |
| **14 — the platform's own by-name calls (§8)** | `term_passing` and `term_passall`, read from Batch 10's fact; `files_search` (517, 556); the volume keys (1539–1543) → a told hook the Sound bar item registers; `dt_kbbar` (`wm/kbd.hibr:72`) dropped, since the bar item reacts on arrival; the hardware watchers' stop (`wm/session.hibr:401–403`) → their own unload hooks; the guards in `wm/context.hibr` and `menubar/clock.hibr`. | `wm/input.hibr`, `wm/menus.hibr`, `wm/session.hibr`, `wm/context.hibr`, `wm/kbd.hibr`, `menubar/clock.hibr` |
| **15 — the remaining L** (after Batch 0) | `cs_ordered` and `dt_btnlist` into a new shared `wm/listfacts.hibr`. `dt_dlgat` stays `ask` carrying `DT_DLGR/C/H/W` until the display layout (`DTW_*`, `dt_dispat`) is mirrored; then `dt_dlgfor` moves too. | `wm/strip.hibr`, `wm/frame.hibr`, `wm/windows.hibr`, `wm/listfacts.hibr` (new) |

Files in both inputs, each in one batch:
- `notifications.hibr` (pane) → Batch 2.
- `keyboard.hibr` and `language.hibr` → Batch 5.
- `files.hibr` → Batch 4.
- `term.hibr` → Batch 10.

Dependencies between batches:
- 14 reads 10's fact.
- 0's `wm/apps.hibr` lines wait on 9's and 12's answers.
- Every L batch waits on 0's `interface.hibr` list.

---

## 8. Seen on the way, in neither input: the platform calling installables by name

`tools/callsites.py` reads only *dynamic* calls. These are literal names, each
behind `dt_has`. Once hosted, each one goes quiet in the desktop with no error:
the ADR 6a failure, from the platform side.

| Kind | Sites |
|---|---|
| Guard, then `dt_launch` (→ FACT) | `menubar/clock.hibr:49` (`clock_draw`); `wm/apps.hibr:859` (`about_draw`); `wm/notify.hibr:545, 801, 811` (`notedetail_draw`, `notifications_draw`); `wm/shot.hibr:278, 280` (`imgview_open`, `files_draw`); `wm/context.hibr:138, 143` (`term_draw`, `tasks_draw`) |
| A real call into an installable | `wm/input.hibr:1369` `term_passing` (**on every input event, status read**); `wm/menus.hibr:476–478` `term_passall` (status read while building menus); `wm/input.hibr:1539–1543` volume keys → Sound's `dt_sndstep`/`dt_sndmutekey`; `wm/kbd.hibr:72` `dt_kbbar`; `wm/apps.hibr:610–611` `cp_themes`/`cp_colourlist` on rescan (Appearance); `wm/apps.hibr:776–777` `mlc_new` for `mailto:`; `wm/handlers.hibr:300` `files_open`; `wm/menus.hibr:517, 556` `files_search`; `wm/session.hibr:401–403` `dt_sndstop`/`dt_netstop`/`dt_btstop` |
| Installed checks | `wm/apps.hibr:878–879` (`dt_kbbar`, `dt_traybar`) → `dt_installed` |

The two that read an answer (`term_passing`, `term_passall`) are not tellable.
They are facts about the terminal window, which the Terminal should push with
its window record.

`callsites.py` should gain a check that fails on a literal name defined only
by an installable, which is the scan in this section.

---

## 9. Costs and limits, said plainly

- An ask is a round trip in the app: typically well under a millisecond, and
  at most a desktop pass. The desktop pays nothing new.
- Redraw-on-arrival makes an app redraw whenever a table it reads moves. The
  clipboard's history moves on every copy, so the Clipboard app redraws on
  every copy. That is correct, and the cost is the app's.
- The survey is a word match. The 36, and the five added here, are a lower
  bound until §5.9 is in.
- `pimset.hibr` is a `# part:` of **both** Calendar and Contacts. "A part goes
  where its owner goes" has two owners here (§10).

---

## 10. Questions for the owner (short)

1. **`dt_openverbs`' `when` predicates** (Archives' `arc_canextract`/`arc_cancompress`).
   Options:
   - (a) declared data — a kind or a test such as "is a regular file" — re-sent
     when Archives' tools change;
   - (b) drop `when`, and let the owner's process decline on activation with a
     note;
   - (c) an asked fan-out the desktop cannot wait on, so not really an option.

   Recommend (a).
2. **Actions on another app's data** (add a contact, answer an invitation):
   - a library writing the shared store, with the owner watching it by
     `dirwatch` (recommended; synchronous, no windowless process); or
   - a request routed to the other app's process, starting it with no window
     if none is running.
3. **Applying a theme as a platform request** (`dt_themeset`), which moves
   about 40 lines of Appearance (catalogue) into the package. The alternative
   is the strip running Appearance's code through a shared library.
4. **Wallpaper and wallset:** one feature split in two panes, joined as parts
   of one process; or two processes, with the selected display a pushed fact.
5. **Power strip:** a library with its own battery reading (two pollers, both
   cheap) versus a fact the bar item publishes through the desktop, which would
   be a new mechanism.
6. **Ask timeout and statuses:** 2000 ms, with 125 for not answered and 126 for
   refused.
7. **`pimset.hibr`, shared as a part by Calendar and Contacts:** which process
   runs it.
