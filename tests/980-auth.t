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
# A PAM that cannot be given a folder of its own -- OpenPAM, macOS's --
# cannot run any of this (Gitea #80): say so with 77 and stop. Only that
# reason skips; any other failure here is the test failing.
case $(auth check -q -c "$d" -s yes "$u" anything 2>&1) in
*"cannot be given a folder of its own"*) echo "skip: this PAM cannot be given a folder of its own"; rm -rf "$d"; exit 77 ;;
esac

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
# -e puts a variable in PAM's environment before the session opens, which is
# where pam_systemd reads XDG_SEAT and XDG_VTNR from (Gitea #367).
auth open -q -c "$d" -s lg -e XDG_SEAT=seat0 -e XDG_VTNR=7 "$u" 'right horse'; echo "open with -e: $?"
auth run sh -c 'echo "seat: $XDG_SEAT vt: $XDG_VTNR"'
auth close
auth open -q -c "$d" -s lg -e nonsense "$u" x 2> /dev/null; echo "-e without =: $?"
auth open -q -c "$d" -s lg root anything; echo "root: $? [$RET]"
auth open -q -c "$d" -s lg no-such-user-here x; echo "nobody: $? [$RET]"

# The password is PAM's token too, set before it authenticates (Gitea #110):
# a module given use_first_pass takes the token and never asks, so a stack
# of those -- macOS's screensaver -- refused the right password when only
# the conversation carried it. Linux-PAM refuses that item from a program
# (its stacks ask), so this runs a stand-in libpam keeping OpenPAM's rule
# from pam_get_authtok.c: a token set is used, none is a refusal, and the
# conversation is never called. It proves the call, its order and its bytes;
# OpenPAM itself is checked on a Mac.
cat > "$d/fp.c" <<'C'
#include <stdlib.h>
#include <string.h>
typedef struct { char *tok; } fp_h;
int pam_start_confdir(const char *svc, const char *user, const void *conv, const char *dir, void **h)
{ *h = calloc(1, sizeof(fp_h)); return 0; }
int pam_start(const char *svc, const char *user, const void *conv, void **h)
{ return pam_start_confdir(svc, user, conv, 0, h); }
int pam_set_item(void *h, int item, const void *v)
{ fp_h *p = h; if (item == 6) { free(p->tok); p->tok = v ? strdup(v) : 0; } return 0; }
int pam_authenticate(void *h, int fl)
{ fp_h *p = h; return p->tok && !strcmp(p->tok, "right horse") ? 0 : 9; }
int pam_acct_mgmt(void *h, int fl) { return 0; }
int pam_setcred(void *h, int fl) { return 0; }
int pam_open_session(void *h, int fl) { return 0; }
int pam_close_session(void *h, int fl) { return 0; }
int pam_putenv(void *h, const char *e) { return 0; }
char **pam_getenvlist(void *h) { return calloc(1, sizeof(char *)); }
int pam_end(void *h, int r) { fp_h *p = h; free(p->tok); free(p); return 0; }
const char *pam_strerror(void *h, int r)
{ fp_h *p = h; return !r ? "Success" : p->tok ? "stand-in: not the token" : "stand-in: no token, and use_first_pass asks nothing"; }
C
printf 'auth required pam_deny.so\naccount required pam_deny.so\n' > "$d/first"
if { tools/tccld -shared -fPIC -o "$d/libpam.so.0" "$d/fp.c" || gcc -shared -fPIC -o "$d/libpam.so.0" "$d/fp.c"; } 2> /dev/null; then
	LD_LIBRARY_PATH="$d" "$HIBR" -c '
mod load ./build/mods/auth.so
auth check -q -c "$1" -s first "$2" "right horse"; echo "token, right: $? [$RET]"
auth check -q -c "$1" -s first "$2" "wrong horse"; echo "token, wrong: $? [$RET]"
auth open -q -c "$1" -s first "$2" "right horse"; echo "token, open: $? [$RET]"
auth close' x "$d" "$u"
else
	echo "no compiler, skipped"
fi
rm -rf "$d"
