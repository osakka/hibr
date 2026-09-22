# POSIX test: argument-count rules, and the grammar above four arguments.
p() { if [ "$@" ]; then echo "true"; else echo "false $?"; fi; }

p
p ''
p x
p ! ''
p ! x
p -n -a
p x = -a
p ! =
p -z ''
p x = x
p 1 -lt 2

p '(' x = x ')'
p '(' -z '' ')'
p ! '(' x = y ')'

p x = x -a y = y
p x = x -a y = z
p 0 -lt 1 -o 0 -gt 2
p 0 -gt 1 -o 0 -gt 2
p -z '' -a ! -z 1
p x = x -a y = z -o w = w
p '(' a = b -o c = c ')' -a d = d
p -z '' -a -z '' -a -z ''
p ! ! -z ''
p 1 -eq 1 -a 2 -eq 2 -a 3 -eq 3 -a 4 -eq 4

p -e /etc/passwd
p -f /etc/passwd -a -d /etc
p -d /etc -o -d /nonexistent
p /etc -ef /etc
p /etc -ef /tmp
p -f /etc/passwd -o -f /nonexistent

# a malformed expression is status 2, not a false
[ x = ] 2>/dev/null; echo "malformed $?"
[ '(' x ] 2>/dev/null; echo "unbalanced $?"
[ x -a ] 2>/dev/null; echo "dangling $?"
