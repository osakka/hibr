# getopts: bundling, attached values, -- , and both error styles.
run() {
  spec=$1; shift
  OPTIND=1
  while getopts "$spec" o "$@" 2>/dev/null; do
    echo "  o=[$o] OPTARG=[$OPTARG] OPTIND=$OPTIND"
  done
  shift $((OPTIND - 1))
  echo "  end OPTIND=$OPTIND rest=[$*]"
}

echo "bundled:";        run abc -abc
echo "bundled+val:";    run ab:c -ac5 -b
echo "attached:";       run c: -c5
echo "separate:";       run c: -c 5
echo "dashdash:";       run ab -a -- -b
echo "nonopt stops:";   run ab -a file -b
echo "long via -:";     run c:-: --help -c 1
echo "unknown loud:";   run ab -x
echo "unknown quiet:";  run :ab -x
echo "missing loud:";   run c: -c
echo "missing quiet:";  run :c: -c
echo "empty:";          run ab
echo "lone dash:";      run ab -
