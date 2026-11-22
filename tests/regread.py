#!/usr/bin/env python3
"""What an installable declares, read and never run (ADR 0050, Decision 3:
dt_regread in programs/desktop/wm/apps.hibr), against what the same file
declares when the desktop sources it, for every installable in this tree.

    python3 tests/regread.py

Each file is loaded twice, each in a desktop of its own with the platform's
files as a session loads them: once sourced (DT_LOADING set, as a scan
sources it) and once read. What each adds to the registries -- the apps,
the Control Panel's panes, the strip's modules, the bar's items, the
openers, the kept settings and each one's value -- must be the same, and
reading must run nothing of the file: no function of its own defined, no
line of its own run. A registration whose words are not written out
cannot be read; each one is named (they are the entries' changes to make:
tools/hostaudit.py) and must be exactly the ones the parser says.
"""
import glob, json, os, re, shutil, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
from screen import check, report, tree
import appstate

HIBR = os.environ.get("HIBR", tree("build/hibr"))
MODS = os.environ.get("HIBR_TESTMODS", tree("build/mods"))
DESK = tree("programs/desktop")
PLAT = [f for f in appstate.platfiles() if not os.path.relpath(f, DESK).startswith(("wm/", "widgets/"))
        and os.path.basename(f) != "desktop.hibr"]
FILES = [f for f in appstate.installables() if f.startswith(DESK + os.sep)] + sorted(
    f for f in glob.glob(os.path.join(DESK, "lib", "*.hibr"))
    if any(re.match(r"^[A-Z]\w*_LIBDONE=1", l) for l in open(f, errors="replace")))
# The registries a registration writes, and whether each is a map.
REGS = "DT_APPS CP_PANES CS_MODS DT_BARI DT_BARIN DT_OPENERS DT_OVN DT_KEEPSTATE DT_APPACT DT_ATSTART DT_BARHOOKS".split()

# The registrations the desktop reads, asked of it (dt_regnames).
_o = subprocess.run([HIBR, "-c", '. "$1" 2> /dev/null; dt_regnames; printf "%s" "$DT_REGNAMES"', "x",
                     os.path.join(DESK, "desktop.hibr")], capture_output=True, text=True,
                    env=dict(os.environ, HIBR_MODPATH=MODS)).stdout
NAMES = json.loads(_o) if _o.startswith("[") else []

# One desktop, loaded as a session loads it, one file loaded the given way,
# and what the registries gained, as JSON; and whether the file ran.
BODY = r'''
DT_HERE=%(desk)s
. %(desk)s/desktop.hibr
for f in %(plat)s; do DT_LOADING=$f . "$f" > /dev/null 2>&1; done
DT_LOADING=
# The libraries an app may source are the desktop's to load first, as a
# session's scan does; a library itself is loaded the way being compared.
case $1 in */lib/*) ;; *) dt_libs > /dev/null 2>&1 ;; esac
before=$(for v in %(regs)s; do [ -n "${!v@a}" ] && json emit "$v" 2> /dev/null; echo; done; printf '%%s\n' "${DT_KEEP[@]}")
fns0=$(declare -F | wc -l)
if [ "$2" = read ]; then
	dt_regread "$1" 2> regread.said > /dev/null
else
	DT_LOADING=$1 . "$1" > /dev/null 2>&1
	DT_LOADING=
fi
for v in %(regs)s; do
	if [ -n "${!v@a}" ]; then json emit "$v" 2> /dev/null; else echo null; fi
	echo
done
echo "@KEEP"
for k in "${DT_KEEP[@]}"; do printf '%%s=%%s\n' "$k" "${!k-}"; done
echo "@FNS $(( $(declare -F | wc -l) - fns0 ))"
[ "$2" != read ] || grep -c "is not written out, so it cannot be read" regread.said
'''


def load(f, how, d):
    script = BODY % {"desk": DESK, "plat": " ".join("'%s'" % p for p in PLAT), "regs": " ".join(REGS)}
    # A file sourced may write where it keeps its things: in a folder of
    # this file's own, the same for both loads, never the tree or the
    # person's.
    env = dict(os.environ, HIBR_MODPATH=MODS, HOME=d, TMPDIR=d, XDG_CONFIG_HOME=d + "/config",
               XDG_STATE_HOME=d + "/state", XDG_DATA_HOME=d + "/data", XDG_CACHE_HOME=d + "/cache")
    p = subprocess.run([HIBR, "-c", script, "x", f, how], capture_output=True, text=True, env=env,
                       timeout=120, cwd=d)
    out = p.stdout
    regs, _, rest = out.partition("@KEEP\n")
    keep, _, fns = rest.partition("@FNS ")
    fl = fns.split()
    return regs, sorted(keep.splitlines()), int(fl[0]) if fl else -1, int(fl[1]) if len(fl) > 1 else 0


def running(f):
    """Whether the file makes a registration from inside a function of its
    own (Archives declares the kinds its tools read, which only running can
    tell): what the desktop cannot read, and leaves to the file's process."""
    names = NAMES
    env = dict(os.environ, HIBR_MODPATH=MODS)
    p = subprocess.run([HIBR, "-c", 'need inspect; inspect calls "$1" -j', "x", f], capture_output=True, text=True, env=env)
    return any(c.get("owner") and c["name"] in names for c in json.loads(p.stdout).get("calls", []))


def dynamic(f):
    """The file's top-level registrations whose words are not written out,
    as the parser reports them: (name, line)."""
    names = NAMES
    env = dict(os.environ, HIBR_MODPATH=MODS)
    p = subprocess.run([HIBR, "-c", 'need inspect; inspect calls "$1" --arguments "$2" -j', "x", f, json.dumps(names)],
                       capture_output=True, text=True, env=env)
    out = []
    for c in json.loads(p.stdout).get("calls", []):
        if not c.get("owner") and c["name"] in names and any(a["quote"] == "dynamic" for a in c.get("arguments", [])):
            out.append((c["name"], c["line"]))
    return out


def one(f):
    d = tempfile.mkdtemp(prefix="regread-")
    try:
        a, b = load(f, "source", d), load(f, "read", d)
    finally:
        shutil.rmtree(d, ignore_errors=True)
    return f, a, b, dynamic(f), running(f)


with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
    res = list(ex.map(one, FILES))

same, differ, ran, unread, byrun = [], [], [], [], []
for f, a, b, dyn, run in res:
    rel = os.path.relpath(f, DESK)
    if b[2] != 0:
        ran.append((rel, b[2]))
    if dyn:
        unread.append((rel, dyn))
        continue
    if (a[0], a[1]) == (b[0], b[1]):
        same.append(rel)
    elif run:
        byrun.append(rel)
    else:
        differ.append((rel, [x for x in a[1] if x not in b[1]][:3], [x for x in b[1] if x not in a[1]][:3]))

print("%d installables: %d read the same as sourced, %d not written out" % (len(FILES), len(same), len(unread)))
for rel in byrun:
    print("  registers from a function of its own, left to its own process: %s" % rel)
for rel, dyn in unread:
    print("  not written out: %s %s" % (rel, ", ".join("%s line %d" % d for d in dyn)))
check("every installable in the tree registers the same read as sourced, registries and kept values alike",
      len(same) > 50 and not differ, differ[:6])
check("and reading runs nothing of the file -- no function of its own is defined -- while it makes "
      "every registration", not ran and len(same) > 50, (ran[:6], len(same)))
said = sorted(os.path.relpath(f, DESK) for f, a, b, dyn, run in res if b[3])
check("what reading says it cannot read is exactly what the parser says is not written out",
      said == sorted(r for r, _ in unread) == ["control-strip/workspaces.hibr"], (said, unread))
check("and what differs registers from a function of its own, which only its own process can run",
      byrun == ["apps/archives.hibr"], byrun)
report(4)
