alias hi='echo hello'
alias count='wc -l'
hi
hi there
alias
printf "a\nb\nc\n" | count
unalias hi
hi 2>/dev/null
echo "after unalias status=$?"
alias ls='echo NOT-REAL-LS'
ls
unalias ls
cd /tmp
pushd /usr >/dev/null
pushd /etc >/dev/null
dirs
popd >/dev/null
pwd
popd >/dev/null
pwd
popd 2>/dev/null
echo "empty pop status=$?"
