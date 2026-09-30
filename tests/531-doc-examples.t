# Every page that says its examples were run is held to it: each ```sh block
# followed by an ```output block is run, in a directory of its own, and its
# output compared. A <!-- setup ... --> comment just before a block is run
# first, unseen by a reader -- the log file or the helper an example assumes.
h=$HIBR
case $h in /*) ;; *) h=$PWD/$h ;; esac
export HIBR_MODPATH=$PWD/build/mods
check() {
  local page=$1 least=$2 n=0 bad=0 state=none src= want= setup= pend= got d line
  while IFS= read -r line; do
    case $state in
    none)
      if [ "$line" = '<!-- setup' ]; then
        state=setup
        pend=
      elif [ "$line" = '```sh' ]; then
        state=sh
        src=
      fi
      ;;
    setup)
      if [ "$line" = '-->' ]; then state=none; setup=$pend; else pend+="$line"$'\n'; fi
      ;;
    sh)
      if [ "$line" = '```' ]; then state=gap; else src+="$line"$'\n'; fi
      ;;
    gap)
      if [ "$line" = '```output' ]; then
        state=out
        want=
      elif [ -n "$line" ]; then
        state=none
        setup=
      fi
      ;;
    out)
      if [ "$line" = '```' ]; then
        n=$((n + 1))
        d=$(mktemp -d)
        got=$(cd "$d" && "$h" -c "$setup$src" 2>&1)
        rm -rf "$d"
        if [ "$got" != "${want%$'\n'}" ]; then
          bad=$((bad + 1))
          echo "example $n in $page does not match what it says:"
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
  done < "$page"
  [ "$n" -ge "$least" ] || echo "only $n examples found in $page, expected at least $least"
  [ "$bad" = 0 ] && echo "every example in $page does what it says"
}
check docs/llm.md 10
check docs/cookbook.md 12
check docs/data.md 10
