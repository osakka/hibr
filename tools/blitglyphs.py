#!/usr/bin/env python3
"""Blit's icon catalogue as the desktop's fallback table.

    tools/blitglyphs.py CATALOGUE.json          write wm/blit-glyphs.json
    tools/blitglyphs.py --check [CATALOGUE.json]    fail if it disagrees

A desktop reads Blit's own catalogue when it can; this table is for the login
screen, a machine without Blit and a Blit on another machine. Generated, never
edited. What Blit has deprecated is left out, so nothing can come to use it.
With --check and no catalogue, the newest Blit package in ~/hibr-apt is read.
"""
import glob, hashlib, json, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "programs", "desktop", "wm", "blit-glyphs.json")


def table(path):
    raw = open(path, "rb").read()
    cat = json.loads(raw)
    glyphs = {}
    for section in ("icons", "text_glyphs", "cell_geometry"):
        for e in cat.get(section, []):
            if not e.get("deprecated"):
                glyphs[e["id"]] = e["codepoint"]
    return {"revision": hashlib.sha256(raw).hexdigest(),
            "glyphs": dict(sorted(glyphs.items()))}


def newest():
    """The newest packaged catalogue's table, or None without a package."""
    debs = glob.glob(os.path.expanduser("~/hibr-apt/pool/main/b/blit/blit_*.deb"))
    if not debs:
        return None
    key = lambda p: [int(x) for x in re.findall(r"\d+", os.path.basename(p))]
    d = tempfile.mkdtemp(prefix="hibr-blit-")
    try:
        subprocess.run(["dpkg-deb", "-x", max(debs, key=key), d], check=True)
        return table(os.path.join(d, "usr/lib/blit/fonts/icons/catalogue.json"))
    finally:
        shutil.rmtree(d, True)


def main():
    a = sys.argv[1:]
    if not a or a == ["-h"] or a == ["--help"]:
        sys.exit(__doc__)
    if a[0] != "--check":
        with open(OUT, "w") as f:
            json.dump(table(a[0]), f, indent=1)
            f.write("\n")
        return
    want = table(a[1]) if len(a) > 1 else newest()
    if not want:
        print("blit-glyphs.json: no Blit package here to compare with")
        return
    have = json.load(open(OUT))
    if want["glyphs"] != have["glyphs"]:
        sys.exit("blit-glyphs.json disagrees with Blit's catalogue: run tools/blitglyphs.py on it")
    print("blit-glyphs.json agrees with Blit's catalogue")


main()
