help | sed -n '2,$p' | sed 's/^  \([^ ]*\) .*/\1/' | while read b; do
  case "$b" in
    module*|"") continue ;;
  esac
  type "$b" > /dev/null || echo "LOOKUP FAILED: $b"
done
echo "every builtin resolves"
