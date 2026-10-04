# keepgoing: an arithmetic error, or nesting too deep, fails only the
# command -- as it does inside try -- rather than ending a script, as it
# does by default and in bash. The desktop turns it on, so one app's slip
# cannot end every window.
h=${HIBR:-./build/hibr}
"$h" -c 'echo $(( 1 + )); echo "not reached"' 2>&1; echo "status $?"
"$h" -c 'shopt -s keepgoing; echo $(( 1 + )); echo "carried on: $?"' 2>&1; echo "status $?"
"$h" -c 'set -o keepgoing; f() { f; }; f; echo "after: $?"' 2>&1 | sed 's/[0-9][0-9]* calls/N calls/'
"$h" -c 'shopt keepgoing; set -o keepgoing; shopt keepgoing; shopt -u keepgoing; shopt keepgoing'
echo done
