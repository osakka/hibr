# Six inputs that ended the shell with SIGSEGV are errors or answers now
# (Gitea #428).
OPTIND=0; set -- -a; getopts a x; echo "getopts $? $x $OPTIND"
set -- b; OPTIND=1; getopts a x; echo "end [$x]"
[[ x == *(*)y ]] && echo y || echo "extglob n"
[[ c == *(?(a))b ]] && echo y || echo "extglob n"
[[ b == +(?(a))b ]] && echo "extglob y" || echo n
declare -n na=nb; declare -n nb=na
na=1 2> /dev/null || echo "circular refused"
declare -n nr; nr=target; target=5; echo "empty nameref $nr"
deep := str repeat "[" 3000
json parse j "$deep" 2> /dev/null || echo "json too deep"
bang := str repeat "!" 5000
( X="${bang}1"; echo $((X)) ) 2> /dev/null || echo "arithmetic too deep"
body := str repeat $':\n' 100000
eval "longf() { ${body} echo long-done; }"
longf
