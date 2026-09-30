# docs/llm.md is written for a model to learn hibr from, so every example in
# it must still do what the page says: each ```sh block followed by an
# ```output block is run, in a directory of its own, and compared.
h=$HIBR
case $h in /*) ;; *) h=$PWD/$h ;; esac
export HIBR_MODPATH=$PWD/build/mods
n=0
bad=0
state=none
src=
want=
while IFS= read -r line; do
  case $state in
  none)
    [ "$line" = '```sh' ] && { state=sh; src=; }
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
    fi
    ;;
  out)
    if [ "$line" = '```' ]; then
      n=$((n + 1))
      d=$(mktemp -d)
      got=$(cd "$d" && "$h" -c "$src" 2>&1)
      rm -rf "$d"
      if [ "$got" != "${want%$'\n'}" ]; then
        bad=$((bad + 1))
        echo "example $n does not match what docs/llm.md says:"
        echo "$src"
      fi
      state=none
    else
      want+="$line"$'\n'
    fi
    ;;
  esac
done < docs/llm.md
[ "$n" -gt 10 ] || echo "only $n examples found"
[ "$bad" = 0 ] && echo "every example in docs/llm.md does what it says"
