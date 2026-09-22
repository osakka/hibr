# The cat module's cardinal rule: in a pipe it is cat, byte for byte. This
# test compares it against /bin/cat directly rather than against bash, since
# bash's cat *is* /bin/cat and the point is the bytes.
mod load ./build/mods/cat.so

d=/tmp/hibr-cat-$$
mkdir -p "$d"
cd "$d" || exit 1
printf 'one\ntwo\nthree\n'          > text
printf 'no trailing newline'        > nonl
: > empty
printf 'a\n\n\n\nb\n\n\nc\n'        > blanks
printf 'tab\there\nctrl\001\002\nhigh\303\251\377\n' > weird
head -c 8000 /bin/sh                > bin

same() {
  /bin/cat "$@" > g.out 2> /dev/null
  gr=$?
  cat "$@" > h.out 2> /dev/null
  hr=$?
  if cmp -s g.out h.out && [ "$gr" -eq "$hr" ]; then
    echo "ok   cat $*"
  else
    echo "FAIL cat $* (status $gr against $hr)"
  fi
}

same text
same nonl
same empty
same blanks
same weird
same bin
same text nonl
same text empty bin
same -n text
same -n blanks
same -b blanks
same -bs blanks
same -bE blanks
same -s blanks
same -E text
same -T weird
same -v weird
same -e weird
same -t weird
same -A weird
same -vT weird
same -vn weird
same -ns text nonl
same nosuchfile
same nosuchfile text
same text nosuchfile
same .
same -- text
same -n -- text

# standard input, including the dash forms
sin() {
  printf 'in1\nin2\n' | /bin/cat "$@" > g.out 2> /dev/null
  printf 'in1\nin2\n' | cat "$@" > h.out 2> /dev/null
  if cmp -s g.out h.out; then echo "ok   stdin cat $*"
  else echo "FAIL stdin cat $*"; fi
}
sin
sin -
sin - -
sin -n -
sin - text

# the real cat is still reachable once the builtin shadows it
command cat text > h.out 2>/dev/null
cmp -s text h.out && echo "ok   command cat reaches a cat"
/bin/cat text > h.out
cmp -s text h.out && echo "ok   /bin/cat still runs"
v := command -v cat; echo "command -v cat says $v"

cd /
rm -rf "$d"
mod drop cat > /dev/null && echo "ok   dropped"
