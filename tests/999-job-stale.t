# A job's number is the desktop's own count and its folder is named by the
# desktop's pid, so a restart -- which keeps the pid -- or a pid used again
# can find a finished job's files from before under the same number. They
# were read as the new job's at once, and dt_jobvarsread evaluated them:
# Administrative Access's pkcheck answers, "com.mesonbuild.install.run 2",
# ran as a command in a release gate. Each job now clears its own files.
r=${TMPDIR:-/tmp}/hibr-jobstale-$$
XDG_RUNTIME_DIR=$r
. programs/desktop/wm/jobs.hibr
declare -g V=
fill() { V=fresh; }
done_cb() { echo "callback: V=$V"; }
dt_jobdir
printf 'com.mesonbuild.install.run 2\n' > "$DTJ_DIR/1.out"
printf '0\n' > "$DTJ_DIR/1.st"
n := dt_jobvars access load done_cb fill V
for i in 1 2 3 4 5 6 7 8 9 10; do
	dt_jobtick
	[ "$DTJ_LIVE" = 0 ] && break
	sleep 0.2
done
rm -rf "$r"
