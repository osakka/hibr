# What a dry run used to do for real (Gitea #432): run a program it planted
# in its own scratch under a reassuring name, run sed's e command, dial with
# connect, copy with -t, and write through $TMPDIR/.. -- each refused and
# recorded now.
p() { "$HIBR" --plan -c "$1" 2>&1 | sed 's/hibr-plan-[A-Za-z0-9]*/SCRATCH/g; s/command line:1: //'; }
p 'cd "$TMPDIR"; cat /bin/sh > cat; chmod 755 cat; ./cat -c "echo ESCAPED"; echo rc=$?'
p 'echo x | sed "1e echo ESCAPED"; echo rc=$?' | grep -v "sandbox mode"
p 'sed -ni p /etc/hostname; echo rc=$?'
p 'cp -t / /etc/hostname; echo rc=$?'
p 'echo x > "$TMPDIR/.."'
p 'connect 127.0.0.1 9; echo rc=$?'
p 'git -c core.pager=cat --version; echo rc=$?'
echo hello | p 'sed s/h/H/'
