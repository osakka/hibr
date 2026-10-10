# Every page is held to its examples. Each ```sh block followed by an
# ```output block is run, in a directory of its own, and its output compared.
# A <!-- setup ... --> comment just before a block is run first, unseen by a
# reader -- the log file or the helper an example assumes. A ```sh block that
# cannot run here (it needs a terminal, a display, root or the network) says
# so in a <!-- not run: why --> line just before it. A code block of any
# other kind says what it is: ```text, ```c, ```ebnf. A bare ``` fence, or an
# example that neither shows its output nor says why not, fails.
h=$HIBR
case $h in /*) ;; *) h=$PWD/$h ;; esac
export HIBR_MODPATH=$PWD/build/mods
fails=0
check() {
  local page=$1 least=$2 n=0 bad=0 state=none src= want= setup= pend= got d
  local line ln=0 at=0 excused=0 prev=
  while IFS= read -r line; do
    ln=$((ln + 1))
    case $state in
    none|gap)
      if [ "$state" = gap ]; then
        if [ "$line" = '```output' ]; then
          state=out
          want=
          continue
        fi
        [ -z "$line" ] && continue
        if [ "$excused" = 0 ]; then
          echo "$page:$at: an example that neither shows its output nor says why not"
          bad=$((bad + 1))
        fi
        state=none
        setup=
      fi
      case $line in
      '<!-- setup')
        state=setup
        pend=
        ;;
      '```sh')
        state=sh
        src=
        at=$ln
        case $prev in '<!-- not run: '*' -->') excused=1 ;; *) excused=0 ;; esac
        ;;
      '```')
        echo "$page:$ln: a code block that does not say what it is"
        bad=$((bad + 1))
        state=other
        ;;
      '```'*)
        state=other
        ;;
      esac
      ;;
    setup)
      if [ "$line" = '-->' ]; then state=none; setup=$pend; else pend+="$line"$'\n'; fi
      ;;
    sh)
      if [ "$line" = '```' ]; then state=gap; else src+="$line"$'\n'; fi
      ;;
    other)
      [ "$line" = '```' ] && state=none
      ;;
    out)
      if [ "$line" = '```' ]; then
        n=$((n + 1))
        d=$(mktemp -d)
        got=$(cd "$d" && "$h" -c "$setup$src" 2>&1)
        rm -rf "$d"
        if [ "$got" != "${want%$'\n'}" ]; then
          bad=$((bad + 1))
          echo "$page:$at: the example does not print what the page says:"
          echo "$src"
          diff <(echo "${want%$'\n'}") <(echo "$got")
        fi
        state=none
        setup=
      else
        want+="$line"$'\n'
      fi
      ;;
    esac
    [ -n "$line" ] && prev=$line
  done < "$page"
  if [ "$state" = gap ] && [ "$excused" = 0 ]; then
    echo "$page:$at: an example that neither shows its output nor says why not"
    bad=$((bad + 1))
  fi
  [ "$n" -ge "$least" ] || { echo "only $n examples run in $page, expected at least $least"; bad=$((bad + 1)); }
  fails=$((fails + bad))
}
check docs/llm.md 10
check docs/cookbook.md 12
check docs/data.md 10
check docs/tutorial.md 9
# Globbed, not named. This list spelled out every page by hand until
# 0.99.138, which is where coverage goes to hide: docs/sheet.md arrived in
# 0.99.120 and was never added, so it was never checked once, although the
# first line of this file says every page is. The four above keep their own
# minimum and are skipped here rather than checked twice.
for page in docs/*.md docs/adr/*.md README.md examples/README.md \
            examples/desktop/README.md examples/desktop/ARCHITECTURE.md \
            mods/README.md mods/*/README.md tests/README.md tools/README.md \
            include/README.md src/README.md; do
  case $page in
  docs/llm.md | docs/cookbook.md | docs/data.md | docs/tutorial.md) continue ;;
  esac
  check "$page" 0
done
[ "$fails" = 0 ] && echo "every example in the documentation does what it says"
