fn cli(...argv) {
  opt -clear
  opt . "Frobnicate the widgets"
  opt -v --verbose  verbose         "Chatty output"
  opt -n --count    count  int=1    "How many times"
  opt -o --output   out    path!    "Where to write"
  opt -t --tag      tags   str+     "A tag, repeatable"
  args "${argv[@]}" || fail -s $? "bad arguments"
  ret "verbose=$verbose count=$count out=$out tags=[${tags[*]}] pos=[${ARGS[*]}]"
}
r := cli -o f.txt;                                    echo "1: $r"
r := cli -v -n 3 -o f.txt a b;                        echo "2: $r"
r := cli -vn3 --output=f.txt --tag x --tag=y c;       echo "3: $r"
r := cli --no-verbose -o f.txt -- -literal;           echo "4: $r"
r := cli --count 7 --output f.txt;                    echo "5: $r"
cli -n 2>/dev/null;                                   echo "missing value status=$?"
cli -n abc -o x 2>/dev/null;                          echo "bad type status=$?"
cli --bogus -o x 2>/dev/null;                         echo "unknown status=$?"
cli 2>/dev/null;                                      echo "required status=$?"
cli -h > /dev/null
echo "help flag=$ARGS_HELP"
try cli --bogus -o x
echo "try caught: err=$ERR status=$ERRSTATUS msg=[$ERRMSG]"
title hibr-worker
read -r c < /proc/$$/comm
echo "comm=$c"
