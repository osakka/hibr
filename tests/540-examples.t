# Every example must at least run, and its --help must come out of its
# own declarations. A rotted example is worse than no example.

tmp=/tmp/hibr-ex-$$
mkdir -p "$tmp"
printf '[server]\nhost = example.com\nport = 8080\n\n[auth]\ntoken = abc123\n' > "$tmp/app.ini"
printf 'xx\n' > "$tmp/one"; printf 'yyy\n' > "$tmp/two"

for e in examples/*.hibr examples/desktop/*.hibr examples/desktop/apps/*.hibr \
         examples/desktop/apps/*/*.hibr examples/desktop/control-panel/*.hibr \
         examples/desktop/desk-accessories/*.hibr examples/desktop/control-strip/*.hibr \
         examples/desktop/wm/*.hibr examples/desktop/widgets/*.hibr \
         examples/desktop/savers/*.hibr examples/desktop/login/*.hibr \
         examples/desktop/lib/*.hibr; do
  case "$e" in *hibrc) continue ;; esac
  ./build/hibr -n "$e" || echo "does not parse: $e"
done
echo "every example parses"

# A function defined twice in a script silently replaces the first, which is
# how the desktop's drag and drop once took over the function that draws the
# open menu. No example, and no app, defines one name twice.
# Both ways of defining one count: name() { and fn name(...) {.
defs='s/^\([A-Za-z_][A-Za-z0-9_]*\)() *{.*/\1/p; s/^fn \([A-Za-z_][A-Za-z0-9_]*\)(.*/\1/p'
for e in examples/*.hibr examples/desktop/*.hibr examples/desktop/apps/*.hibr \
         examples/desktop/apps/*/*.hibr examples/desktop/control-panel/*.hibr \
         examples/desktop/desk-accessories/*.hibr examples/desktop/control-strip/*.hibr; do
  sed -n "$defs" "$e" | sort | uniq -d |
    while read -r f; do echo "defined twice in $e: $f"; done
done
# The desktop is desktop.hibr and every file it sources, so a name must be
# unique across all of them together, not only within each: two parts that
# each define it once still leave only the second one standing.
cat examples/desktop/desktop.hibr examples/desktop/wm/*.hibr \
    examples/desktop/widgets/*.hibr |
  sed -n "$defs" | sort | uniq -d |
  while read -r f; do echo "defined twice across the desktop's parts: $f"; done
echo "no function is defined twice"

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
for e in examples/*.hibr examples/desktop/*.hibr examples/desktop/apps/*.hibr \
         examples/desktop/apps/*/*.hibr examples/desktop/control-panel/*.hibr \
         examples/desktop/desk-accessories/*.hibr examples/desktop/control-strip/*.hibr \
         examples/desktop/wm/*.hibr examples/desktop/widgets/*.hibr; do
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
desk=$(find examples/desktop -name '*.hibr' | sort)
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
    W["stash"] = 2; W["resume"] = 1; W["idle"] = 1
    P["draw"] = 4; P["key"] = 2; P["click"] = 3; P["drop"] = 4; P["wheel"] = 2
    S["draw"] = 2; S["click"] = 2
  }' $desk $desk
echo "every callback names what it is called with"

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
panes=$(grep -l '^command -v cp_pane' examples/desktop/control-panel/*.hibr)
kept=$( (grep -rhoE 'dt_keep( [A-Z_][A-Z0-9_]*)+' examples/desktop \
          --include='*.hibr' | sed 's/^dt_keep//'
        sed -n '/^DT_KEEP=(/,/)/p' examples/desktop/wm/settings.hibr |
          tr -d '()' | sed 's/DT_KEEP=//') | tr ' \t' '\n\n' |
        grep -E '^[A-Z_][A-Z0-9_]*$' | sort -u)
for v in $kept; do
  case $v in CS_Y | CS_LEN | CS_COLLAPSED | CP_DIVCOL | FP_LISTW | SS_TRUST | WPK_DIR | DT_SETVER) continue ;; esac
  grep -qw "$v" $panes || echo "kept but in no Control Panel pane: $v"
done
echo "every kept preference is in the Control Panel"

# The window manager, its widgets and every app, pane, desk accessory and
# strip module run under every strict check -- all but session.hibr, which
# is the user's own script to write as they like, so a function defined twice, a
# forgotten local and an expansion that splits are each refused rather than
# silently wrong. Anything that splits a list does it explicitly, with
# read -ra into an array.
for f in examples/desktop/desktop.hibr \
         examples/desktop/wm/*.hibr examples/desktop/widgets/*.hibr \
         examples/desktop/apps/*.hibr examples/desktop/apps/*/*.hibr \
         examples/desktop/desk-accessories/*.hibr \
         examples/desktop/control-panel/*.hibr \
         examples/desktop/control-strip/*.hibr \
         examples/desktop/savers/*.hibr examples/desktop/login/*.hibr \
         examples/desktop/lib/*.hibr; do
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
for e in examples/desktop/apps/*.hibr examples/desktop/control-panel/*.hibr \
         examples/desktop/desk-accessories/*.hibr; do
  sed -e :a -e '/\\$/N; s/\\\n[[:space:]]*/ /; ta' "$e" | awk -v f="$e" '
    /^[[:space:]]*#/ { next }
    /console put[[:space:]]/ && !/console put[[:space:]]+-p[[:space:]]/ {
      print f ": console put without -p: " $0
    }
    /(^|[^A-Za-z_])(img|term) draw[[:space:]]/ && !/[[:space:]]-p[[:space:]]/ {
      print f ": draw without -p: " $0
    }
  '
done
echo "no window content draws at absolute screen coordinates"

# A glyph the desktop draws is named in GL (wm/glyphs.hibr), never written
# into the code -- see CLAUDE.md. What stays literal: an app's or a pane's
# icon, a menu's or a note's own words (an ellipsis, a dash), and lists
# that are themselves a choice of characters -- the wallpaper's, the
# bell's.
for e in examples/desktop/*.hibr examples/desktop/*/*.hibr \
         examples/desktop/*/*/*.hibr; do
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
roles=$(grep -oE '^DT_[A-Z_]+="\$\{DT_[A-Z_]+-#' examples/desktop/wm/settings.hibr |
	sed 's/=.*//' | sort -u)
grep -rhoE 'console pen( -p [^ ]+)? "\$DT_[A-Z_]+"( "\$DT_[A-Z_]+")?' \
	--include=*.hibr examples/desktop | grep -oE 'DT_[A-Z_]+' | sort -u |
	while read -r v; do
		printf '%s\n' "$roles" | grep -qx "$v" ||
			echo "a pen uses $v, which no theme defines"
	done
echo "every pen names a theme colour"
