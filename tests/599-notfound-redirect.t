# A command that is not found reports it through the command's own
# redirections, so the familiar `cmd 2>/dev/null || fallback` is quiet.
{ nosuchcommand-hibr 2>/dev/null; } 2>&1 | wc -l
{ nosuchcommand-hibr; } 2>&1 | wc -l
nosuchcommand-hibr 2>/dev/null || echo "fallback taken, status kept"
{ nosuchcommand-hibr 2>/dev/null; echo "st=$?"; }
