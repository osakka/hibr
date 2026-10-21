# logtime: the shell's own diagnostics carry the local time they were
# written, RFC 3339, for a log read later (Gitea #329). Off by default, the
# same in both spellings, kept by a child, and never in agent mode's JSON.
stamp='^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}[+-][0-9]{2}:[0-9]{2} hibr: '
line() { "$HIBR" -c "$1" 2>&1 >/dev/null | head -1; }
l=$(line 'nosuch_217')
[[ $l =~ $stamp ]] && echo "off: stamped" || echo "off: $l"
l=$(line 'set -o logtime; nosuch_217')
[[ $l =~ $stamp ]] && echo "set -o: stamped, ${l#* }" || echo "set -o: $l"
l=$(line 'shopt -s logtime; nosuch_217')
[[ $l =~ $stamp ]] && echo "shopt: stamped" || echo "shopt: $l"
l=$(line 'shopt -s logtime; ( nosuch_217 )')
[[ $l =~ $stamp ]] && echo "subshell: stamped" || echo "subshell: $l"
l=$(line 'shopt -s logtime; x=$(nosuch_217)')
[[ $l =~ $stamp ]] && echo "substitution: stamped" || echo "substitution: $l"
l=$(line 'set -o logtime; set +o logtime; nosuch_217')
echo "off again: $l"
l=$(TZ=UTC "$HIBR" -c 'set -o logtime; nosuch_217' 2>&1)
[[ $l == *"+00:00 hibr: "* ]] && echo "utc: +00:00" || echo "utc: $l"
l=$(TZ=Asia/Kolkata "$HIBR" -c 'set -o logtime; nosuch_217' 2>&1)
[[ $l == *"+05:30 hibr: "* ]] && echo "half hour: +05:30" || echo "half hour: $l"
l=$(TZ=America/New_York "$HIBR" -c 'set -o logtime; nosuch_217' 2>&1)
[[ $l == *"-0"[45]":00 hibr: "* ]] && echo "west: negative" || echo "west: $l"
l=$(line 'set -o agent; set -o logtime; nosuch_217')
[ "${l:0:1}" = "{" ] && echo "agent: JSON, unstamped" || echo "agent: $l"
shopt logtime
set -o | grep logtime
