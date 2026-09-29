# HIBR is this shell's own absolute path, however it was started.
case $HIBR in
/*) echo absolute ;;
*) echo "not absolute: $HIBR" ;;
esac
[ -x "$HIBR" ] && echo executable
[ "$("$HIBR" -c 'echo "$HIBR_VERSION"')" = "$HIBR_VERSION" ] && echo same shell
d=${HIBR%/*}
[ "$(cd "$d" && ./"${HIBR##*/}" -c 'echo "$HIBR"')" = "$HIBR" ] && echo relative start
[ "$(env PATH="$d:$PATH" HIBR=elsewhere "${HIBR##*/}" -c 'echo "$HIBR"')" = "$HIBR" ] && echo found on PATH
[ -z "$("$HIBR" -c hash 2>&1 | grep -v 'no commands')" ] && echo nothing remembered
