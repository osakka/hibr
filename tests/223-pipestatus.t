# PIPESTATUS after each kind of command, compared against bash (0.99.404):
# hibr never set it before. A pipeline of one -- a command, a function call,
# an assignment, a subshell, [[ ]] or (( )) -- sets it to its own status; a
# compound command does not, only the pipelines inside it; ! keeps the status
# it negates.
false | true | (exit 3); echo "a [${PIPESTATUS[@]}]"
false | true; if true; then false; fi; echo "b [${PIPESTATUS[@]}]"
false | true; while false; do :; done; echo "c [${PIPESTATUS[@]}]"
f() { false | true; }; f; echo "d [${PIPESTATUS[@]}]"
false | true; x=1; echo "e [${PIPESTATUS[@]}]"
false | true; { (exit 4); }; echo "f [${PIPESTATUS[@]}]"
false | true; ! true; echo "g [${PIPESTATUS[@]}]"
false | (exit 2); a=("${PIPESTATUS[@]}"); echo "h [${a[@]}] [${PIPESTATUS[@]}] n=${#PIPESTATUS[@]}"
false | true; echo "i ${PIPESTATUS[1]} ${PIPESTATUS[0]}"
false | true; [[ 1 == 2 ]]; echo "j [${PIPESTATUS[@]}]"
true | false; (( 0 )); echo "k [${PIPESTATUS[@]}]"
false | true; case x in y) ;; esac; echo "l [${PIPESTATUS[@]}]"
false | true; for i in; do :; done; echo "m [${PIPESTATUS[@]}]"
false | true; echo "n $PIPESTATUS"
g() { return 5; }; g | cat; echo "o [${PIPESTATUS[@]}]"
true | true | true | true | false; echo "p [${PIPESTATUS[*]}] ${#PIPESTATUS[@]}"
false | true; echo "q ${!PIPESTATUS[@]}"
# Commands that leave early: not found, a failing redirection, a substitution.
false | true; nosuchcmd 2>/dev/null; echo "r [${PIPESTATUS[@]}]"
false | true; { true > /nonexistent/x; } 2>/dev/null; echo "s [${PIPESTATUS[@]}]"
false | true; x=$(exit 7); echo "t [${PIPESTATUS[@]}]"
false | true; : "${undefined_xyz}"; echo "u [${PIPESTATUS[@]}]"
false | true; [ -z "" ]; echo "v [${PIPESTATUS[@]}]"
false | true; set -- a b; echo "w [${PIPESTATUS[@]}]"
false | true; f(){ :; }; echo "x [${PIPESTATUS[@]}]"
false | true; cd /nonexistent 2>/dev/null; echo "y [${PIPESTATUS[@]}]"
