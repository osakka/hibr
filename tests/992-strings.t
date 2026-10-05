# Every string the desktop draws for a person is in the catalogue's list,
# examples/desktop/lang/strings.txt (#68): a string added or changed in the
# code without `tools/strings.py --write` fails here, before a translation
# goes stale. Recorded: the list is hibr's own.

python3 tools/strings.py --check | sed 's/([0-9]*)$/(n)/'
