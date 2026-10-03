# exec -a names the program, -l makes it a login shell's name, -c gives it
# no environment, as in bash
( exec -a myname sh -c 'echo "$0"' )
( exec -l sh -c 'echo "$0"' )
( exec -l -a ksh sh -c 'echo "$0"' )
( exec -la ksh2 sh -c 'echo "$0"' )
( FOO=1; export FOO; exec -c /usr/bin/env ) | wc -l
( exec -- sh -c 'echo dashdash' )
( exec sh -c 'echo plain "$0"' )
( exec -x sh -c 'echo no' ) 2>/dev/null; echo "bad option: $?"
