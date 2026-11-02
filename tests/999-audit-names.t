shopt -s expand_aliases
d=$(mktemp -d); cd "$d"
echo "-- alias"
alias a="echo 'x'"; alias a; eval "$(alias a)"; alias a
alias nope 2>/dev/null; echo "alias nope $?"
unalias nope 2>/dev/null; echo "unalias nope $?"
alias b=1 c=2; unalias -a; alias | wc -l
alias z=zz y=yy; alias -p
echo "-- command -v of an alias"
alias q="echo 'it''s'"; command -v q
echo "-- type"
type -t cd; type -t ls; type -t nosuch; echo "rc $?"
f() { :; }; type -t f; type -t if
type -p ls >/dev/null; echo "type -p $?"; type -P cd; echo "type -P cd $?"
alias ll='ls -l'; type -t ll
echo "-- command"
command -- echo hi
command -pv sh >/dev/null; echo "command -pv $?"
echo "-- pushd and dirs"
mkdir p1 p2; cd p1; pushd ../p2 >/dev/null; pushd >/dev/null; dirs | sed "s|$d|D|g"; dirs -c; dirs | sed "s|$d|D|g"
pushd -n ../p2 >/dev/null; dirs -v | sed "s|$d|D|g"
echo "-- cd"
cd ""; echo "cd empty $?"
OLDPWD=/nonexistent/x; cd - 2>/dev/null; echo "cd - failed $?"
echo "-- more of the stack"
cd "$d"; mkdir -p s1 s2 s3; dirs -c
pushd s1 >/dev/null; pushd ../s2 >/dev/null; pushd ../s3 >/dev/null
dirs -p | sed "s|$d|D|g"; dirs +1 | sed "s|$d|D|g"; dirs -0 | sed "s|$d|D|g"
pushd +2 | sed "s|$d|D|g"; pwd | sed "s|$d|D|g"
popd +1 | sed "s|$d|D|g"; popd -n | sed "s|$d|D|g"; popd | sed "s|$d|D|g"; popd 2>/dev/null >/dev/null; echo "empty $?"
dirs +9 2>/dev/null; echo "out of range $?"
pushd 2>/dev/null >/dev/null; echo "nothing to swap $?"
echo "-- type -a, -P, -f"
type -t -a ls | sort -u; type -P ls | head -1 | sed 's|.*/||'
f() { :; }; type -f f 2>/dev/null; echo "type -f f $?"
echo "-- from the review"
cd "$d"; mkdir -p rel; OLDPWD=rel; cd - >/dev/null; echo "relative cd - $?"; pwd | sed 's|.*/||'; cd "$d"
( PATH=/usr/bin:/bin:/nonexist; export PATH; command -p printenv PATH )
( command -p export PATH=/foo; echo "$PATH" )
command -v ls nosuch >/dev/null; echo "command -v any found $?"
type -p nosuch; echo "type -p nosuch $?"; type -at echo; type -P /bin/ls
cd /; rm -rf "$d"
