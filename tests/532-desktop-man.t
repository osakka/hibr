# The desktop's man page cannot drift from the code (Gitea #344): every
# option session.hibr declares and every request lib/ctl.hibr sends is in
# docs/desktop.1.in, and every request the page lists under CONTROL is one
# the client sends. Rendered with groff's warnings on, where groff is here.
page=docs/desktop.1.in
man=$(< "$page")
# Options are looked for where each is explained, not in the synopsis.
opts=${man#*$'\n'.SH OPTIONS$'\n'}
opts=${opts%%$'\n'.SH *}
miss=0
while IFS= read -r l; do
  if [[ $l =~ ^opt\ -[a-zA-Z]\ --([a-z]+) ]]; then
    o=${BASH_REMATCH[1]}
    if [[ $opts != *"\\-\\-$o"* ]]; then
      echo "option --$o is not explained in the page"
      miss=1
    fi
  fi
done < programs/desktop/session.hibr
ops=
while IFS= read -r l; do
  if [[ $l =~ ^$'\t'([a-z]+(\ [|]\ [a-z]+)*)\) ]]; then
    for w in ${BASH_REMATCH[1]//|/ }; do
      ops+=" $w"
    done
  fi
done < programs/desktop/lib/ctl.hibr
echo "requests the client sends:$ops"
for w in $ops; do
  if ! [[ $man =~ $'\n'\.B[IR]?\ \"?$w[\ \"$'\n'] ]]; then
    echo "request $w is not in the page"
    miss=1
  fi
done
sec= prev=
while IFS= read -r l; do
  case $l in
  .SH\ *) sec=${l#.SH } ;;
  esac
  if [ "$sec" = CONTROL ] && [ "$prev" = .TP ]; then
    w=${l#.B* }
    w=${w#\"}
    w=${w%%[ \"]*}
    case " $ops " in
    *" $w "*) ;;
    *) echo "the page lists $w, which the client does not send"; miss=1 ;;
    esac
  fi
  prev=$l
done < "$page"
[ "$miss" = 0 ] && echo "every option and request is in the page, and nothing more"
if command -v groff > /dev/null; then
  w=$(groff -man -ww -z "$page" 2>&1)
  [ -z "$w" ] && echo "groff renders it with no warning" || echo "groff: $w"
else
  echo "groff renders it with no warning"
fi
