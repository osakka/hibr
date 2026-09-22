# ${@:off:len} and ${*:off:len} select parameters, they do not slice a string.
set -- alpha "two words" gamma delta

echo "star     [${*:2}]"
echo "at       [${@:2}]"
echo "star-len [${*:2:2}]"
echo "at-len   [${@:1:1}]"
echo "from-0   [${*:0:1}]"
echo "negative [${*: -2}]"
echo "neg-len  [${*: -3:2}]"
echo "past-end [${*:99}]"
echo "zero-len [${*:1:0}]"
i=3; echo "byvar    [${*:i}]"
echo "arith    [${*:1+1:1}]"

# quoting decides how many fields come out
n() { echo "  n=$# :: $*"; }
n "${@:2}"
n ${@:2}
n "${*:2}"
n "${@:2:1}"

# and it still works inside a function, on that function's parameters
f() { echo "fn [${*:2}] n=$#"; g() { echo "  inner [${@:1:2}]"; }; g "$@"; }
f one two three four
