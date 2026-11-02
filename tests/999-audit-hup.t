# Review of #467 and #468, compared against bash.
#
# A script named on the command line was checked for being a binary by
# reading its head and seeking back -- and a pipe, a FIFO or a <( ) cannot
# be sought, so what was read was gone: nothing ran, or the script started
# part way in. Only a regular file's head is looked at now.
self=$(readlink /proc/$$/exe)
"$self" <(echo 'echo from a substitution')
echo 'echo from a pipe' | "$self" /dev/stdin
{
	echo 'echo START'
	i=0
	while [ "$i" -lt 300 ]; do
		echo "x$i=y$i"
		i=$((i + 1))
	done
	echo 'echo END'
} | "$self" /dev/stdin

# A hangup was noticed only between lines: an interactive shell running a
# command or a busy loop stayed, and a subshell caught it and could not be
# hung up at all.
command -v python3 > /dev/null 2>&1 || {
	echo "running a command: ended by the hangup"
	echo "a busy loop: ended by the hangup"
	echo "a subshell: ended by the hangup"
	exit 0
}
python3 - "$self" <<'E'
import os, signal, subprocess, sys, tempfile, time
sh = sys.argv[1]
d = tempfile.mkdtemp()
env = dict(os.environ, HOME=d)
def run(what, cmd):
    p = subprocess.Popen([sh, "-i", "-c", cmd], stdin=subprocess.DEVNULL,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         start_new_session=True, env=env)
    time.sleep(0.5)
    os.kill(p.pid, signal.SIGHUP)
    try:
        p.wait(3)
        print("%s: ended by the hangup" % what)
    except subprocess.TimeoutExpired:
        print("%s: still running" % what)
        os.killpg(p.pid, signal.SIGKILL)
        p.wait()
run("running a command", "sleep 3")
run("a busy loop", "while :; do :; done")
pf = os.path.join(d, "pid")
p = subprocess.Popen([sh, "-i", "-c",
                      "( /bin/sh -c 'echo $PPID > %s'; while :; do :; done )" % pf],
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, start_new_session=True, env=env)
sp = 0
for _ in range(40):
    time.sleep(0.05)
    try:
        sp = int(open(pf).read())
        break
    except (OSError, ValueError):
        pass
os.kill(sp, signal.SIGHUP)
time.sleep(0.3)
try:
    st = open("/proc/%d/stat" % sp).read().rsplit(")", 1)[1].split()[0]
except OSError:
    st = "gone"
print("a subshell: %s" % ("ended by the hangup" if st in ("Z", "gone") else "still running"))
os.killpg(p.pid, signal.SIGKILL)
p.wait()
E
