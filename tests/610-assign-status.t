# A command with no command word takes the status of its last substitution.
V=$(exit 7);       echo "cmdsub      $?"
V=`exit 4`;        echo "backquote   $?"
V=plain;           echo "plain       $?"
V=;                echo "empty       $?"
A=$(exit 1) B=$(exit 2); echo "last-wins   $?"
V=$(exit 3) W=y;   echo "then-plain  $?"
V=x$(exit 5)y;     echo "embedded    $?"
V=$(exit 6)        # a trailing comment must not change it
echo "commented   $?"

# a command word means the command's status, not the substitution's
true $(exit 9);    echo "with-cmd    $?"
V=$(exit 8) true;  echo "prefix      $?"

# assignment builtins keep their own status, as everywhere
f() { local q=$(exit 5); echo "local       $?"; }
f
export E=$(exit 6); echo "export      $?"

# nested and arithmetic
V=$(echo $(exit 2); exit 3); echo "nested      $?"
V=$((1+1));        echo "arith       $?"

# it feeds set -e
(set -e; V=$(exit 1); echo "errexit did not fire") 2>/dev/null; echo "errexit     $?"
(set -e; V=$(exit 0); echo "zero is fine"); echo "zero        $?"

# an array assignment is still a command with no command word
a=($(exit 3));             echo "array       $?"
b=($(echo x; exit 4));     echo "array2      $? [${b[*]}]"

# and the value still lands
V=$(echo hello; exit 4); echo "value [$V] status $?"
