# Every example must at least run, and its --help must come out of its
# own declarations. A rotted example is worse than no example.

tmp=/tmp/hibr-ex-$$
mkdir -p "$tmp"
printf '[server]\nhost = example.com\nport = 8080\n\n[auth]\ntoken = abc123\n' > "$tmp/app.ini"
printf 'xx\n' > "$tmp/one"; printf 'yyy\n' > "$tmp/two"

for e in examples/*.hibr programs/*.hibr programs/desktop/*.hibr programs/desktop/apps/*.hibr \
         programs/desktop/apps/*/*.hibr programs/desktop/control-panel/*.hibr \
         programs/desktop/system/*.hibr programs/desktop/control-strip/*.hibr \
         programs/desktop/wm/*.hibr programs/desktop/widgets/*.hibr \
         programs/desktop/savers/*.hibr programs/desktop/login/*.hibr \
         programs/desktop/lib/*.hibr; do
  case "$e" in *hibrc) continue ;; esac
  ./build/hibr -n "$e" || echo "does not parse: $e"
done
echo "every example parses"

# A function defined twice in a script silently replaces the first, which is
# how the desktop's drag and drop once took over the function that draws the
# open menu. No example, and no app, defines one name twice.
# Both ways of defining one count: name() { and fn name(...) {.
defs='s/^\([A-Za-z_][A-Za-z0-9_]*\)() *{.*/\1/p; s/^fn \([A-Za-z_][A-Za-z0-9_]*\)(.*/\1/p'
for e in examples/*.hibr programs/*.hibr programs/desktop/*.hibr programs/desktop/apps/*.hibr \
         programs/desktop/apps/*/*.hibr programs/desktop/control-panel/*.hibr \
         programs/desktop/system/*.hibr programs/desktop/control-strip/*.hibr; do
  sed -n "$defs" "$e" | sort | uniq -d |
    while read -r f; do echo "defined twice in $e: $f"; done
done
# The desktop is desktop.hibr and every file it sources, so a name must be
# unique across all of them together, not only within each: two parts that
# each define it once still leave only the second one standing.
cat programs/desktop/desktop.hibr programs/desktop/wm/*.hibr \
    programs/desktop/widgets/*.hibr |
  sed -n "$defs" | sort | uniq -d |
  while read -r f; do echo "defined twice across the desktop's parts: $f"; done
echo "no function is defined twice"

# The catalogue command and its window share one prefix, cat_ and CAT_.
# The appstore -> catalogue rename in 0.99.216 converted the window and
# left the command on ap_/AP_ -- 982 identifiers, from when it was
# called `apps` -- so anyone grepping for one prefix found half of it
# (Gitea #266). The old one must not come back in either file.
grep -nE '\b(ap|AP)_[A-Za-z0-9_]+' programs/catalogue.hibr \
     programs/desktop/system/catalogue.hibr |
  while read -r l; do echo "the catalogue's old ap_ prefix is back: $l"; done
echo "the catalogue uses one prefix"

# local id=$1 b=${ARR[$id]...} looks right and often runs right: a local
# statement's own words are all expanded before local assigns any of
# them, so a later word reading an earlier word's name in the same
# statement reads whatever it meant before the statement ran, not what
# was just assigned. Masked whenever the caller's own local happens to
# share the name -- which "id" alone did in seven places across two files
# before this check found them, every one shipped and passing real use
# for as long as nothing called it from a context with no such "id" lying
# around. Cheap and approximate (whitespace-split, so a quoted value with
# a space in it is not read correctly), not exhaustive -- see CLAUDE.md's
# own trap entry for the pattern in full.
for e in examples/*.hibr programs/*.hibr programs/desktop/*.hibr programs/desktop/apps/*.hibr \
         programs/desktop/apps/*/*.hibr programs/desktop/control-panel/*.hibr \
         programs/desktop/system/*.hibr programs/desktop/control-strip/*.hibr \
         programs/desktop/wm/*.hibr programs/desktop/widgets/*.hibr; do
  awk '
  /^[[:space:]]*local[[:space:]]/ {
    line = $0
    sub(/^[[:space:]]*local[[:space:]]+/, "", line)
    sub(/[[:space:]]*#.*/, "", line)
    n = split(line, words, /[[:space:]]+/)
    delete seen
    for (i = 1; i <= n; i++) {
      w = words[i]
      if (w == "") continue
      eq = index(w, "=")
      if (eq == 0) { seen[w] = 1; continue }
      name = substr(w, 1, eq - 1)
      val = substr(w, eq + 1)
      for (nm in seen) {
        pat = "\\$\\{?" nm "([^A-Za-z0-9_]|$)"
        if (val ~ pat)
          print FILENAME ":" FNR ": local " name "=" val \
            " reads " nm ", assigned earlier in the same local statement"
      }
      seen[name] = 1
    }
  }' "$e"
done
echo "no local reads a name assigned earlier in the same statement"

# The window manager calls a callback with a fixed set of arguments -- a
# window's _draw with id h w row col, its _click with id r c btn -- and a
# declared function refuses one it has no name for, so a callback that
# names fewer than it is handed never runs. 0.44 shipped seven of these,
# on paths the suites never reached. Which prefix is a window, a pane or a
# strip module is read from the desktop's own dt_app, dt_new, cp_pane and
# cs_module calls.
desk=$(find programs/desktop -name '*.hibr' | sort)
awk '
  FNR == 1 { if (!first) first = FILENAME; if (FILENAME == first) pass++ }
  pass == 1 && !/^[[:space:]]*#/ {
    if (match($0, /dt_app [a-z_]+/)) win[substr($0, RSTART + 7, RLENGTH - 7)] = 1
    if (match($0, /cp_pane [a-z_]+/)) pane[substr($0, RSTART + 8, RLENGTH - 8)] = 1
    if (match($0, /cs_module [a-z_]+/)) strip[substr($0, RSTART + 10, RLENGTH - 10)] = 1
    if ($1 ~ /dt_new$/ || / := dt_new /) { w = $NF; gsub(/"/, "", w); win[w] = 1 }
    next
  }
  pass == 2 && /^fn [a-z_]+_[a-z]+\(/ {
    name = $2; sub(/\(.*/, "", name)
    suf = name; sub(/.*_/, "", suf); pre = substr(name, 1, length(name) - length(suf) - 1)
    params = $0; sub(/^[^(]*\(/, "", params); sub(/\) \{.*/, "", params)
    if (params ~ /\.\.\./) next
    n = (params == "") ? 0 : split(params, a, ",")
    need = ""
    if (pre in win) need = W[suf]
    else if (pre in pane) need = P[suf]
    else if (pre in strip) need = S[suf]
    if (need != "" && n < need)
      print FILENAME ": " name " names " n " of the " need " arguments it is called with"
  }
  BEGIN {
    W["open"] = 2; W["close"] = 1; W["draw"] = 5; W["key"] = 2; W["click"] = 4
    W["mouse"] = 6; W["wheel"] = 4; W["drop"] = 5; W["context"] = 1
    W["stash"] = 2; W["resume"] = 1; W["idle"] = 1; W["hover"] = 3
    # Copy, Cut and Paste, which had no entry here until Gitea #250: a
    # paste is the text and, third, cut or copy, and `term_paste` was
    # declared `op` rather than `op = ""` -- the one of twenty-three
    # that could not take the short call wm/input.hibr was making, so a
    # paste from the host terminal into a terminal window did nothing.
    W["paste"] = 3; W["copy"] = 1; W["cut"] = 1; W["awake"] = 1
    # The new top of a scrollbar being held, from the window manager
    # (Gitea #298).
    W["scroll"] = 3
    P["draw"] = 4; P["key"] = 2; P["click"] = 3; P["drop"] = 4; P["wheel"] = 2
    P["slide"] = 3
    S["draw"] = 2; S["click"] = 2
  }' $desk $desk
echo "every callback names what it is called with"

# dt_new calls the new window's <prefix>_open, so a function of that name
# which opens its own kind of window calls itself until the stack runs out:
# 0.99.24's mail account dialog had an opener named mlad_open, and the
# desktop died with a segfault the moment Add Account was chosen.
awk '
  FNR == 1 { if (!first) first = FILENAME; if (FILENAME == first) pass++ }
  pass == 1 && !/^[[:space:]]*#/ {
    if (match($0, /dt_app [a-z_]+/)) win[substr($0, RSTART + 7, RLENGTH - 7)] = 1
    if ($1 ~ /dt_new$/ || / := dt_new /) { w = $NF; gsub(/"/, "", w); win[w] = 1 }
    next
  }
  pass == 2 && /^(fn )?[a-z_]+_open\(/ {
    cur = $0; sub(/^fn /, "", cur); sub(/_open\(.*/, "", cur)
    if (!(cur in win)) cur = ""
    next
  }
  pass == 2 && /^}/ { cur = ""; next }
  pass == 2 && cur != "" && /dt_new / {
    w = $NF; gsub(/"/, "", w)
    if (w == cur) print FILENAME ": " cur "_open opens a " cur " window, which calls " cur "_open again"
  }' $desk $desk
echo "no window's _open opens another of itself"

# Every app's bar reads File, Edit, then whatever that app adds, so the two
# menus a person reaches for most are in the same place in every window
# (Gitea #120). An app with menus of its own therefore opens with File and
# calls dt_editmenu before its second menu; an app with none gets both from
# the window manager. Ten apps opened with their own menu and had Edit last
# before this rule, which is the kind of thing nobody notices one app at a
# time. dt_menus itself is the window manager's bar and is not an app.
awk '
  FNR == 1 { if (!first) first = FILENAME; if (FILENAME == first) pass++ }
  pass == 1 && !/^[[:space:]]*#/ {
    if (match($0, /dt_app [a-z_]+/)) win[substr($0, RSTART + 7, RLENGTH - 7)] = 1
    next
  }
  pass == 2 && /^(fn )?[a-z_]+_menus\(/ {
    pre = $0; sub(/^fn /, "", pre); sub(/_menus\(.*/, "", pre)
    if (!(pre in win)) pre = ""
    nm = 0; edited = 0; next
  }
  pass == 2 && pre != "" && /^}/ { pre = ""; next }
  pass == 2 && pre != "" && /^[[:space:]]*dt_editmenu$/ { edited = 1; next }
  pass == 2 && pre != "" && /^[[:space:]]*dt_menu / {
    nm++
    t = $0; sub(/^[^"]*"/, "", t); sub(/".*/, "", t)
    if (nm == 1 && t != "File")
      print FILENAME ": " pre "_menus opens with " t ", not File"
    if (nm == 2 && !edited)
      print FILENAME ": " pre "_menus declares " t " before it calls dt_editmenu"
  }' $desk $desk
echo "every app's bar begins File, then Edit"

# Every row of the Edit menu works in every app or is dimmed there, since
# the desktop draws the menu from the callbacks an app has (Gitea #372).
# So the only way one is missing by accident is an app that never wrote
# it. This table is the apps that leave a row out on purpose, and each
# group says why; an app missing a row not named here fails, and so does
# a row named here that the app has since grown. An app is a file with an
# `app` line and a _draw.
awk '
  BEGIN {
    # Nothing in the window is text to take or put back: games, and the
    # windows that are only switches and lists of things to run or install.
    split("bricks mines puzzle snake panel catalogue prayer", a, " ")
    for (i in a) NOT[a[i]] = "copy cut paste selall undo redo"
    # The Vault copies a secret with its own buttons, kept off the history.
    NOT["vault"] = "copy cut paste selall undo redo"
    # Shows things and copies what is chosen, and edits nothing in place:
    # an edit is a dialog of its own, with fields that undo for themselves.
    split("about calendar clock contacts imgview mail modman pdfr tasks", a, " ")
    for (i in a) NOT[a[i]] = "cut paste selall undo redo"
    NOT["notifications"] = "paste selall undo redo"
    NOT["clipboard"] = "selall undo redo"
    # Hand what is pasted to a program or a page, which keeps its own
    # history; the window has none to undo.
    split("term browser youtube calc", a, " ")
    for (i in a) NOT[a[i]] = "cut selall undo redo"
    NOT["xscreen"] = "copy cut selall undo redo"
    NOT["dbase"] = "copy cut selall undo redo"
  }
  FNR == 1 { pre = ""; drew = 0 }
  /^app [a-z]+ "/ && pre == "" { pre = $2 }
  /^(fn )?[a-z_]+\(/ {
    nm = $0; sub(/^fn /, "", nm); sub(/\(.*/, "", nm)
    have[FILENAME, nm] = 1
  }
  { file[FILENAME] = pre }
  END {
    split("copy cut paste selall undo redo", ops, " ")
    for (f in file) {
      pre = file[f]
      if (pre == "" || !((f, pre "_draw") in have)) continue
      for (k = 1; k <= 6; k++) {
        op = ops[k]
        on = ((f, pre "_" op) in have)
        meant = (" " NOT[pre] " ") ~ (" " op " ")
        if (!on && !meant) print f ": " pre " has no _" op " and the table does not say why"
        if (on && meant) print f ": " pre "_" op " exists, so the table is out of date"
      }
    }
  }' $(find programs/desktop/apps programs/desktop/system -name '*.hibr' | sort)
echo "every Edit row an app leaves out is one it means to"

# Every setting kept across restarts -- the desktop's own DT_KEEP, and every
# app's dt_keep -- is one the Control Panel shows, so nothing an app lets you
# choose is only reachable by editing the settings file. A pane is a file
# that registers one with cp_pane. What is kept but is not a preference is
# named here with its reason: where the strip sits, how long it is and
# whether it is folded are set by dragging and clicking the strip itself,
# where a split window's divider sits by dragging the divider, which
# sheets are trusted by the Sheet menu's own switch,
# WPK_DIR is only where the wallpaper picker was last left, and DT_SETVER
# is which version of the settings wrote the file.
panes=$(grep -l '^command -v cp_pane' programs/desktop/control-panel/*.hibr)
kept=$( (grep -rhoE 'dt_keep( [A-Z_][A-Z0-9_]*)+' programs/desktop \
          --include='*.hibr' | sed 's/^dt_keep//'
        sed -n '/^DT_KEEP=(/,/)/p' programs/desktop/wm/settings.hibr |
          tr -d '()' | sed 's/DT_KEEP=//') | tr ' \t' '\n\n' |
        grep -E '^[A-Z_][A-Z0-9_]*$' | sort -u)
for v in $kept; do
  # State rather than choice, each for a reason: where the Control Strip
  # sits and how long it is, whether it is collapsed, where the Control
  # Panel's, the file dialog's and the diagram editor's dividers were left,
  # which sheets this machine trusts (never in a sheet, so one cannot
  # arrive trusted), the wallpaper picker's last folder, where YouTube's
  # mini player was last left, and the settings file's own version.
  case $v in CS_Y | CS_LEN | CS_COLLAPSED | CP_DIVCOL | FP_LISTW | DIA_SPLIT | CAT_SPLIT | SS_TRUST | WPK_DIR | YT_MINIGEO | DT_SETVER) continue ;; esac
  grep -qw "$v" $panes || echo "kept but in no Control Panel pane: $v"
done
echo "every kept preference is in the Control Panel"

# A pane that offers a dropdown offers it for every row that is one. A `set`
# row opens its popup only when the pane's own <pane>_drop has a case for
# that row's key (panel.hibr asks dt_has "${pane}_drop" and then hands it
# the key); a key with no case falls through the case statement, no popup
# opens, and the row quietly degrades to cycling on enter and the arrows --
# which is what 0.99.150's own new row did, reported as "the drop down does
# not drop down, it's only on return it changes" (Gitea #186). Nothing said
# so: the row drew, the setting worked, and only the popup was missing.
for f in $panes; do
  n=$(basename "$f" .hibr)
  grep -q "fn ${n}_drop" "$f" || continue
  drop=$(sed -n "/fn ${n}_drop/,/^}/p" "$f")
  # Only a `set` row becomes a dropdown: an `action` is a button and a
  # `head` is a heading, and panel.hibr excludes a bool (a checkbox) and a
  # slider before it asks for a popup. The kind belongs to the row the key
  # is on, so the rows are walked in order rather than the keys grepped out.
  for k in $(awk '
      /\["kind"\]=/ { k = $0; sub(/.*\["kind"\]=/, "", k); sub(/[;" \t].*/, "", k); kind = k; wt = "" }
      /\["wtype"\]=/ { w = $0; sub(/.*\["wtype"\]=/, "", w); sub(/[;" \t].*/, "", w); wt = w }
      /\["key"\]=/ {
        key = $0; sub(/.*\["key"\]=/, "", key); sub(/[;" \t].*/, "", key)
        if (kind == "set" && wt != "bool" && wt != "slider" && key ~ /^[a-z]/) print key
      }' "$f" | sort -u); do
    # A case label may be the last of several -- `locktime | syncevery |
    # clear)` -- so it is followed by a `)` or by a `|`, and matching only
    # the first shape called two handled rows unhandled.
    grep -qE "(^|[|( 	])$k[ 	]*[|)]" <<< "$drop" ||
      echo "a dropdown row with no case in ${n}_drop: $k"
  done
done
echo "every dropdown row has its popup"

# The window manager, its widgets and every app, pane, desk accessory and
# strip module run under every strict check -- all but session.hibr, which
# is the user's own script to write as they like, so a function defined twice, a
# forgotten local and an expansion that splits are each refused rather than
# silently wrong. Anything that splits a list does it explicitly, with
# read -ra into an array.
for f in programs/desktop/desktop.hibr \
         programs/desktop/wm/*.hibr programs/desktop/widgets/*.hibr \
         programs/desktop/apps/*.hibr programs/desktop/apps/*/*.hibr \
         programs/desktop/system/*.hibr \
         programs/desktop/control-panel/*.hibr \
         programs/desktop/control-strip/*.hibr \
         programs/desktop/savers/*.hibr programs/desktop/login/*.hibr \
         programs/desktop/lib/*.hibr; do
  grep -qx 'strict' "$f" || echo "not under every strict check: $f"
done
echo "the desktop is strict"

# A window's own content must never draw at absolute screen coordinates --
# console put without -p, or img/term draw without -p anywhere in the call --
# which is exactly what reaching around the pane system by reading
# DT[$id]["row"]/["col"] directly used to look like: a real photo drawn
# straight at the wrong position, or a terminal that renders outside its own
# window entirely once the module underneath it changes shape. Excludes
# control-strip/*.hibr and desktop.hibr itself: the control strip, the
# wallpaper, the menu bar, dt_note and dt_confirm are the desktop's own root
# overlay, not a window's content, and have no pane of their own to target.
for e in programs/desktop/apps/*.hibr programs/desktop/apps/*/*.hibr \
         programs/desktop/control-panel/*.hibr \
         programs/desktop/system/*.hibr; do
  sed -e :a -e '/\\$/N; s/\\\n[[:space:]]*/ /; ta' "$e" | awk -v f="$e" '
    /^[[:space:]]*#/ { next }
    # -p may come after other flags: `console put -r -p "w$id"` is paned,
    # and reading only the word after `put` called it absolute. The hole
    # was invisible until this check began covering apps/*/ at all --
    # Office, Internet and Games were never scanned for this before
    # 0.99.126, which is the kind of gap a glob list hides.
    /console put[[:space:]]/ && !/console put([[:space:]]+-[A-Za-z]+)*[[:space:]]+-p[[:space:]]/ {
      print f ": console put without -p: " $0
    }
    /(^|[^A-Za-z_])(img|term) draw[[:space:]]/ && !/[[:space:]]-p[[:space:]]/ {
      print f ": draw without -p: " $0
    }
  '
done
echo "no window content draws at absolute screen coordinates"

# A menu opened at the pointer is given a screen row, and a literal one is
# right only while the primary display -- the one with the bar -- is at the
# top of the screen. With two displays stacked and the primary below, the
# sound menu given row 1 opened on the display above the bar, the way the
# bar's own menus did until 0.99.253: a menu under the bar asks dt_ptop, or
# for a bar item dt_barmenu, never a number.
for e in programs/desktop/wm/*.hibr programs/desktop/widgets/*.hibr \
         programs/desktop/apps/*.hibr programs/desktop/apps/*/*.hibr \
         programs/desktop/control-panel/*.hibr programs/desktop/system/*.hibr \
         programs/desktop/control-strip/*.hibr; do
  awk -v f="$e" '
    /^[[:space:]]*#/ { next }
    /dt_context_open[[:space:]]+[^[:space:]]+[[:space:]]+[^[:space:]]+[[:space:]]+[0-9]+([[:space:]]|$)/ {
      print f ": a menu at a literal row: " $0
    }
  ' "$e"
done
echo "no menu is opened at a literal row"

# A glyph the desktop draws is named in GL (wm/glyphs.hibr), never written
# into the code -- see CLAUDE.md. What stays literal: an app's or a pane's
# icon, a menu's or a note's own words (an ellipsis, a dash), and lists
# that are themselves a choice of characters -- the wallpaper's, the
# bell's.
for e in programs/desktop/*.hibr programs/desktop/*/*.hibr \
         programs/desktop/*/*/*.hibr; do
  case $e in */wm/glyphs.hibr) continue ;; esac
  sed -e :a -e '/\\$/N; s/\\\n[[:space:]]*/ /; ta' "$e" | LC_ALL=C awk -v f="$e" '
    /^[[:space:]]*#/ { next }
    /dt_app |cp_pane |dt_item |dt_dim |dt_sub |dt_mark |dt_menu |dt_note|dt_confirm |dbase_say / { next }
    /GLYPHS=\(|DT_GLYPH=|icon:-/ { next }
    f ~ /wm\/bar.hibr$/ && /ret "/ { next }
    {
      l = $0
      gsub(/\342\200\246|\342\200\224/, "", l)
      if (l ~ /[\200-\377]/) print f ": a glyph written, not named: " $0
    }
  '
done
echo "every glyph drawn is named"

for e in examples/fetch.hibr examples/ls-report.hibr examples/conf.hibr examples/workers.hibr; do
  ./build/hibr "$e" --help > /dev/null 2>&1 || echo "no --help: $e"
done
echo "every declared program answers --help"

./build/hibr examples/conf.hibr --file "$tmp/app.ini" -s auth
./build/hibr examples/conf.hibr -f "$tmp/app.ini" -s server -k port
./build/hibr examples/workers.hibr -n 2 -j 2 | tail -1
./build/hibr examples/ls-report.hibr --dir "$tmp" --top 1 | head -1 |
  sed "s|$tmp|DIR|"

rm -rf "$tmp"

# A pen names a theme's colour or one of its roles, never a variable no
# theme sets: the YouTube app's selected row once asked for $DT_SELECT,
# which nothing defines, and drew dark text on an empty -- black --
# background. Every $DT_ name a console pen uses must be one settings.hibr
# gives a default.
roles=$(grep -oE '^DT_[A-Z_]+="\$\{DT_[A-Z_]+-#' programs/desktop/wm/settings.hibr |
	sed 's/=.*//' | sort -u)
grep -rhoE 'console pen( -p [^ ]+)? "\$DT_[A-Z_]+"( "\$DT_[A-Z_]+")?' \
	--include=*.hibr programs/desktop | grep -oE 'DT_[A-Z_]+' | sort -u |
	while read -r v; do
		printf '%s\n' "$roles" | grep -qx "$v" ||
			echo "a pen uses $v, which no theme defines"
	done
echo "every pen names a theme colour"

# The Catalogue window does not keep its own list of kinds -- `apps`
# sends them with `list -j` and cat_load takes them (Gitea #220, after
# three releases in which `mods` existed in one list and not the other).
# What the window still owns is what each kind is *called*, so the check
# that is left is that every kind `apps` declares has a name here. A
# kind with none falls back to its own short word, which is legible and
# is not English anybody chose, so this names it rather than failing the
# build.
#
# CAT_KINDS is wrapped over two lines, so the file is flattened first and
# the list taken from `CAT_KINDS=(` to the first `)`. A sed line range
# cannot do it: `/NAME=(/,/)/` never ends on its own opening line, so a
# list written on one line swallows whatever follows it.
apk=$(tr '\n\t' '  ' < programs/catalogue.hibr |
	sed 's/.*CAT_KINDS=(//; s/).*//; s/  */ /g; s/^ //; s/ $//')
[ -n "$apk" ] || echo "could not read CAT_KINDS"
grep -q '^CAT_KINDS=()$' programs/desktop/system/catalogue.hibr ||
	echo "the Catalogue keeps a kind list of its own again"
for k in $apk; do
	grep -q "^	$k) ret " programs/desktop/system/catalogue.hibr ||
		echo "the Catalogue has no name for the kind $k"
done
echo "every kind apps declares has a name in the Catalogue"

# A dialog opens where the person is working: dt_dlgat, over the focused
# window on its own display (Gitea #293). Centred on DT_ROWS and DT_COLS
# it opened between two displays or on another one -- some thirty dialogs
# did, and one placed off-screen is how it was found.
grep -rnE --include=*.hibr '\(\(\(?DT_ROWS *(/ *2 *-|- *[a-z0-9]+\) */ *2)' \
	programs/desktop | grep -v ':[[:space:]]*#' |
	sed 's/^/centred on the whole screen, not with dt_dlgat: /'
echo "every dialog is placed with dt_dlgat"

# A window's _wheel is told up, down, left or right: the window manager
# strips "wheel" from the action before it calls one (wm/input.hibr).
# Contacts and Diagram matched wheelup and the rest, so their wheels had
# never moved anything (Gitea #298). Only the window manager itself, and
# the workspaces it switches on the empty desktop, see the raw action.
grep -rnE --include=*.hibr '^[[:space:]]*wheel(up|down|left|right)[ |)]' \
	programs/desktop | grep -v '^programs/desktop/wm/\(input\|workspaces\)\.hibr:' |
	sed 's/^/a _wheel matching the raw action, which it is never handed: /'
echo "every _wheel matches what it is handed"
