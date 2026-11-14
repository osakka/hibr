# lang add: an app's own translations added to the catalogue loaded, as
# the desktop does for each lang/<code>/*.json an app ships (0.99.523).
# Recorded: the module is hibr's own.

mod load ./build/mods/lang.so || exit 1
lang add tests/lang/app-ar.json 2> /dev/null; echo "with nothing loaded: $?"
lang load tests/lang/ar-test.json
n0=$(lang info)
lang add tests/lang/app-fr.json 2> /dev/null; echo "another language: $?"
lang add tests/lang/app-bad.json 2> /dev/null; echo "a broken file: $?"
lang has Broken; echo "it left a key: $?"
[ "$(lang info)" = "$n0" ] && echo "and the catalogue is as it was"
lang add tests/lang/app-ar.json; echo "added: $?"
lang get Save
lang get "Open Page"
for n in 1 2 5; do printf '%s: ' "$n"; lang plural "$n" "%d tabs" "$n"; done
lang info | cut -f1-3
