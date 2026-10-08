# HIBR_MODULES says whether a module may answer a command nothing else did,
# and from which end of the search: off, after (the default), before. The
# collision is made here rather than borrowed from the machine -- a program
# named upper on PATH, and the sys module's own upper builtin -- so the
# answer is the setting's and not the box's. Recorded: the three words and
# the default are hibr's own (ADR 0040).
export HIBR_MODPATH=./build/mods
d=$(mktemp -d)
printf '#!/bin/sh\necho "the program"\n' > "$d/upper"
chmod +x "$d/upper"
PATH="$d:$PATH"
export PATH

echo "--- after, the default: the program on PATH wins"
"$HIBR" -c 'upper hello'

echo "--- before: the module wins over the program"
HIBR_MODULES=before "$HIBR" -c 'upper hello'

echo "--- off: no autoload, and the program is still found"
HIBR_MODULES=off "$HIBR" -c 'upper hello'

echo "--- after, with nothing on PATH to find"
PATH=/nonexistent "$HIBR" -c 'upper hello'

echo "--- off, with nothing on PATH either"
PATH=/nonexistent HIBR_MODULES=off "$HIBR" -c 'upper hello' 2>&1

# Twice, and correctly: an assignment prefix is in force for the shell
# resolving *this* command too, so the shell running this test reads it
# while looking up "$HIBR" itself, and then the child reads it again. That
# is the same mechanism that makes `HIBR_MODULES=before ls` mean "the
# module, for this one command".
echo "--- a word that is none of the three says so, and behaves as after"
PATH=/nonexistent HIBR_MODULES=sideways "$HIBR" -c 'upper hello' 2>&1

echo "--- the setting can change inside the shell"
"$HIBR" -c 'upper a; HIBR_MODULES=before; upper b'

# Already loaded is already a builtin: the setting governs autoloading, not
# what a module that is in memory shadows -- which is the whole meaning of
# `mod load`, and has always been so.
echo "--- a module already loaded stays a builtin whatever the setting says"
HIBR_MODULES=before "$HIBR" -c 'upper a; HIBR_MODULES=after; upper b'

echo "--- a plan never autoloads: a module init may do anything"
PATH=/nonexistent "$HIBR" --plan -c 'upper hello' 2>&1

echo "--- a builtin is still the builtin, whatever the setting says"
HIBR_MODULES=before "$HIBR" -c 'echo echo is the builtin'

echo "--- and a function still wins over both"
HIBR_MODULES=before "$HIBR" -c 'upper() { echo "the function"; }; upper hi'

echo "--- the flag says the same thing for one invocation"
"$HIBR" --modules=before -c 'upper hello'
"$HIBR" --modules=off -c 'upper hello'
PATH=/nonexistent "$HIBR" --modules=off -c 'upper hello' 2>&1

echo "--- and the flag is not exported to what the shell runs"
"$HIBR" --modules=before -c '"$HIBR" -c "upper hello"' 2>&1
rm -rf "$d"
