#!/bin/sh
# Run every tests/*.t. A test with a matching .expected file is compared
# against that recording (stdout, stderr and exit status), because its
# behaviour is deliberately ours. Every other test is compared against a
# reference shell, stdout and exit status only, since error wording differs.

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
cd "$root" || exit 1

HIBR=${HIBR:-./build/hibr}
REF=${REF:-bash}
verbose=0
filter=

for a in "$@"; do
	case $a in
	-v) verbose=1 ;;
	-h|--help)
		echo "usage: tests/run.sh [-v] [name-prefix]"
		echo "  HIBR=path   shell under test   (default ./build/hibr)"
		echo "  REF=path   reference shell    (default bash)"
		exit 0
		;;
	*) filter=$a ;;
	esac
done

if [ ! -x "$HIBR" ]; then
	echo "no shell at $HIBR -- run make first" >&2
	exit 1
fi

pass=0
fail=0
skip=0
failed=

for t in "$here"/*.t; do
	name=$(basename "$t" .t)
	if [ -n "$filter" ]; then
		case $name in
		$filter*) ;;
		*) continue ;;
		esac
	fi
	exp="$here/$name.expected"
	if [ -f "$exp" ]; then
		got=$("$HIBR" "$t" 2>&1)
		grc=$?
		want=$(sed -n '2,$p' "$exp")
		wrc=$(sed -n '1p' "$exp")
		mode=recorded
	else
		if ! command -v "$REF" >/dev/null 2>&1; then
			skip=$((skip + 1))
			printf 'SKIP %s (no %s)\n' "$name" "$REF"
			continue
		fi
		got=$("$HIBR" "$t" 2>/dev/null)
		grc=$?
		want=$("$REF" "$t" 2>/dev/null)
		wrc=$?
		mode=$REF
	fi
	if [ "$got" = "$want" ] && [ "$grc" = "$wrc" ]; then
		pass=$((pass + 1))
		[ "$verbose" = 1 ] && printf 'ok   %-18s (%s)\n' "$name" "$mode"
	else
		fail=$((fail + 1))
		failed="$failed $name"
		printf 'FAIL %-18s (%s)\n' "$name" "$mode"
		printf '  exit: got %s want %s\n' "$grc" "$wrc"
		printf '%s\n' "$want" > "/tmp/hibr-want-$name"
		printf '%s\n' "$got" > "/tmp/hibr-got-$name"
		diff -u "/tmp/hibr-want-$name" "/tmp/hibr-got-$name" |
			sed -n '3,20p' | sed 's/^/  /'
	fi
done

printf '\n%d passed, %d failed' "$pass" "$fail"
[ "$skip" -gt 0 ] && printf ', %d skipped' "$skip"
printf '\n'
[ "$fail" -gt 0 ] && printf 'failed:%s\n' "$failed"
[ "$fail" -gt 0 ] && exit 1
exit 0
