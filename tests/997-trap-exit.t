# exit inside a trap ends the shell with its own status, and a bare exit
# with the status of the command before the trap; nothing after it runs.
# hibr put the earlier status back after every trap, so a desktop stopped
# by SIGTERM exited 1 instead of 143.

for c in 'trap "exit 143" TERM; kill -TERM $$; sleep 1; echo not reached' \
         'f() { exit 7; }; trap f TERM; kill -TERM $$; echo not reached' \
         'trap "exit" TERM; true; kill -TERM $$; echo not reached' \
         'trap "exit" TERM; false; kill -TERM $$; echo not reached' \
         'trap "echo caught" TERM; kill -TERM $$; false; echo "status kept: $?"' \
         'trap "echo in trap" HUP; kill -HUP $$; echo "carries on"'; do
  "$SH" -c "$c"
  echo "status $?"
done
