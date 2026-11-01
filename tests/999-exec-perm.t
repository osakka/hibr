# A command named by a path that is not a file which may be run is refused
# the way bash refuses it: "Permission denied" and 126 for a file without
# the execute bit, "Is a directory" and 126 for a folder, "Not a directory"
# and 126 for a path through a file, and "No such file or directory" and
# 127 only when nothing is there. hibr said "command not found" and 127 for
# all but the folder. Compared against bash.

d=$(mktemp -d)
cd "$d" || exit 1
printf 'echo ran\n' > plain
chmod 644 plain
printf '#!/bin/sh\necho ran\n' > prog
chmod 755 prog
mkdir sub

./plain 2>/dev/null; echo "plain: $?"
"$d/plain" 2>/dev/null; echo "absolute: $?"
./sub 2>/dev/null; echo "folder: $?"
./plain/x 2>/dev/null; echo "through a file: $?"
./missing 2>/dev/null; echo "missing: $?"
./prog; echo "prog: $?"
command ./plain 2>/dev/null; echo "command: $?"
command ./missing 2>/dev/null; echo "command missing: $?"
./plain 2>/dev/null > out; echo "redirected: $? $(test -f out && echo made)"
type ./sub >/dev/null 2>&1; echo "type folder: $?"
command -v ./plain; echo "-v plain: $?"
command -v ./prog
( exec ./plain 2>/dev/null ); echo "exec: $?"

err=$(./plain 2>&1)
case $err in *"Permission denied"*) echo "says permission denied" ;; *) echo "says: $err" ;; esac
err=$(./sub 2>&1)
case $err in *"Is a directory"*) echo "says is a directory" ;; *) echo "says: $err" ;; esac
err=$(./missing 2>&1)
case $err in *"No such file or directory"*) echo "says no such file" ;; *) echo "says: $err" ;; esac

command_not_found_handle() { echo "handler: $1"; return 9; }
command_not_found() { echo "handler: $1"; return 9; }
./plain 2>/dev/null; echo "with a handler: $?"
cd / && rm -rf "$d"
