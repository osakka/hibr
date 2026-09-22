# command -v: the portable way to ask whether something is runnable.
myfn() { :; }

command -v cat;        echo "  external rc=$?"
command -v echo;       echo "  builtin  rc=$?"
command -v myfn;       echo "  function rc=$?"
command -v while;      echo "  keyword  rc=$?"
command -v nope-nope;  echo "  missing  rc=$?"
command -V echo;       echo "  verbose  rc=$?"

# the two shapes real scripts use
command -v cat >/dev/null 2>&1 && echo "  guard taken"
if ! command -v nope-nope >/dev/null 2>&1; then echo "  negation taken"; fi

# it must not run the command
command -v false; echo "  did not run it, rc=$?"

# a path with a slash is reported as given when it is executable
command -v /bin/sh; echo "  absolute rc=$?"
