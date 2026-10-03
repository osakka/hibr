# auth: password checks through PAM, given a folder of PAM configuration
# of its own (-c) so nothing here touches a real account's password:
# "yes" takes anything, "no" refuses everything, and "pw" takes only
# "right horse", checked by a pam_exec script. Recorded, since bash has
# no auth to compare against.
mod load ./build/mods/auth.so

d=$(mktemp -d)
printf 'auth required pam_permit.so\naccount required pam_permit.so\n' > "$d/yes"
printf 'auth required pam_deny.so\naccount required pam_permit.so\n' > "$d/no"
printf '#!/bin/sh\ntr -d "\\000" | grep -qx "right horse"\n' > "$d/chk.sh"
chmod +x "$d/chk.sh"
printf 'auth required pam_exec.so expose_authtok quiet %s/chk.sh\naccount required pam_permit.so\n' "$d" > "$d/pw"
u=$(id -un)

auth check -c "$d" -s yes "$u" anything; echo "yes: $?"
auth check -q -c "$d" -s no "$u" anything; echo "no: $? [$RET]"
auth check -c "$d" -s no "$u" anything 2>&1 | sed "s/$u/USER/"
auth check -q -c "$d" -s pw "$u" 'right horse'; echo "right: $?"
auth check -q -c "$d" -s pw "$u" 'wrong horse'; echo "wrong: $?"
p='right horse'
auth check -q -c "$d" -s pw "$u" "$p"; echo "from a variable: $? [$p]"
auth check 2>&1; echo "usage: $?"
auth check -c "$d" "$u" 2>&1; echo "no password: $?"
auth bogus 2> /dev/null; echo "unknown: $?"
auth ready; echo "ready: $?"
w := auth whoami; [ "${w%%	*}" = "$u" ]; echo "whoami names this user: $?"
case $(auth service) in hibr|login|screensaver) echo "service: known" ;; *) echo "service: odd" ;; esac

# a login: open checks and opens a session, run runs a command in it with
# the environment PAM gives (pam_env here), close ends it. Not as root,
# run can only be this same user.
printf 'auth required pam_exec.so expose_authtok quiet %s/chk.sh\nauth optional pam_permit.so\naccount required pam_permit.so\nsession required pam_permit.so\nsession optional pam_env.so readenv=0 conffile=%s/env.conf\n' "$d" "$d" > "$d/lg"
echo 'AUTH_TEST_VAR DEFAULT=from-pam' > "$d/env.conf"
auth open -q -c "$d" -s lg "$u" wrong; echo "open, wrong: $?"
auth run true 2>&1; echo "run with nothing open: $?"
auth open -q -c "$d" -s lg "$u" 'right horse'; echo "open: $?"
auth open -q -c "$d" -s lg "$u" 'right horse' 2>&1; echo "open twice: $?"
auth run sh -c 'echo "as self: $([ "$(id -un)" = "$LOGNAME" ] && echo yes) home: $([ "$PWD" = "$HOME" ] && echo yes) pam: $AUTH_TEST_VAR"; exit 3'; echo "run: $?"
auth run no-such-program-here 2> /dev/null; echo "missing: $?"
auth close; echo "close: $?"
auth close; echo "close twice: $?"
auth open -q -c "$d" -s lg root anything; echo "root: $? [$RET]"
auth open -q -c "$d" -s lg no-such-user-here x; echo "nobody: $? [$RET]"
rm -rf "$d"
