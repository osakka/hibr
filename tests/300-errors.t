fn boom(str why) { fail "exploded: $why"; ret never; }
try boom disk
echo "msg=[$ERRMSG] err=$ERR status=$ERRSTATUS"
try true
echo "ok err=$ERR msg=[$ERRMSG]"
trap 'echo "ERR trap fired"' ERR
false
echo "continued"
trap - ERR
false
echo "trap cleared"
fail -s 7 "custom status"
echo "fail status=$? msg=$ERRMSG"
set -e
try boom net
echo "try shields errexit"
echo 99999999999999999>/dev/null; echo "huge-fd-prefix status=$?"
echo 9999999999 > /dev/null; echo "huge-number-word status=$?"
2>/dev/null echo "normal fd redirection still parses"
