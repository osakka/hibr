# A malformed ${...} after =~ is a syntax error, not a crash in the parser.
eval '[[ x =~ ${^(a) ]]' 2>/dev/null; echo "status $?"
eval '[[ x =~ ${ ]]' 2>/dev/null; echo "status $?"
echo "still running"
