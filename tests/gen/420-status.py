import zlib, hashlib, struct

out = []
def oct_escape(b): return "".join("\\0%03o" % c for c in b)
def emit(path, data):
    e = oct_escape(data)
    ch = [e[i:i+320] for i in range(0, len(e), 320)]
    out.append("printf '%s' > %s" % (ch[0], path))
    for c in ch[1:]:
        out.append("printf '%s' >> %s" % (c, path))

def mkobj(t, body):
    raw = t.encode() + b" " + str(len(body)).encode() + b"\0" + body
    return hashlib.sha1(raw).hexdigest(), raw

objs = {}
def add(t, body):
    sha, raw = mkobj(t, body)
    objs[sha] = zlib.compress(raw, 6)
    return sha

def tree(entries):
    b = b""
    for mode, name, sha in sorted(entries, key=lambda e: e[1] + ("/" if e[0] == "40000" else "")):
        b += mode.encode() + b" " + name.encode() + b"\0" + bytes.fromhex(sha)
    return add("tree", b)

def commit(t, parents, msg, ts):
    b = b"tree " + t.encode() + b"\n"
    for p in parents:
        b += b"parent " + p.encode() + b"\n"
    b += b"author A <a@e> %d +0000\n" % ts
    b += b"committer A <a@e> %d +0000\n\n%s\n" % (ts, msg.encode())
    return add("commit", b)

def index(entries, version=2):
    """entries: list of (mode, sha, stage, path); mtime/size taken from content"""
    b = b"DIRC" + struct.pack(">II", version, len(entries))
    for mode, sha, stage, path, size in sorted(entries, key=lambda e: (e[3], e[2])):
        e = struct.pack(">IIIIIIIII", 0, 0, 0, 0, 0, 0, mode, 0, 0)
        e += struct.pack(">I", size)
        e += bytes.fromhex(sha)
        nl = len(path.encode())
        flags = (stage << 12) | min(nl, 0xFFF)
        e += struct.pack(">H", flags)
        e += path.encode() + b"\0"
        while (len(e) + 8) % 8:
            e += b"\0"
        pad = ((62 + nl + 8) & ~7) - (62 + nl)
        e = e[:62 + nl] + b"\0" * pad
        b += e
    b += hashlib.sha1(b).digest()
    return b

# --- content ---
a_body = b"alpha\n"
b_body = b"bravo\n"
c_body = b"charlie\n"
a_sha = add("blob", a_body)
b_sha = add("blob", b_body)
c_sha = add("blob", c_body)

t_ab = tree([("100644", "a.txt", a_sha), ("100644", "b.txt", b_sha)])
c1 = commit(t_ab, [], "one", 1700000000)
c2 = commit(t_ab, [c1], "two", 1700000100)
c3 = commit(t_ab, [c2], "three", 1700000200)
r1 = commit(t_ab, [c1], "remote one", 1700000050)

out.append("mod load ./build/mods/prompt.so")
out.append('d=/tmp/nsh-stat-$$')
out.append('rm -rf "$d"')
out.append('mkdir -p "$d/r/.git/refs/heads" "$d/r/.git/refs/remotes/origin"')
for sha, z in objs.items():
    out.append('mkdir -p "$d/r/.git/objects/%s"' % sha[:2])
    emit('"$d/r/.git/objects/%s/%s"' % (sha[:2], sha[2:]), z)
out.append("printf 'ref: refs/heads/main\\n' > \"$d/r/.git/HEAD\"")
out.append("printf '%s\\n' > \"$d/r/.git/refs/heads/main\"" % c3)
out.append("printf 'alpha\\n' > \"$d/r/a.txt\"")
out.append("printf 'bravo\\n' > \"$d/r/b.txt\"")
emit('"$d/r/.git/index"', index([(0o100644, a_sha, 0, "a.txt", len(a_body)),
                                 (0o100644, b_sha, 0, "b.txt", len(b_body))]))
out.append('cd "$d/r"')

out.append('fn st() { p := prompt status; echo "$p"; }')
out.append('echo "== clean"')
out.append('st')
out.append('echo "== modified"')
out.append('printf \'changed\\n\' > a.txt')
out.append('st')
out.append('printf \'alpha\\n\' > a.txt')
out.append('echo "== deleted"')
out.append('rm b.txt')
out.append('st')
out.append('printf \'bravo\\n\' > b.txt')
out.append('echo "== untracked, then ignored"')
out.append('printf \'x\\n\' > u.txt')
out.append('st')
out.append("printf 'u.txt\\n' > .gitignore")
out.append('echo "-- u.txt ignored, but .gitignore itself is untracked"')
out.append('st')
out.append("printf 'u.txt\\n.gitignore\\n' > .gitignore")
out.append('echo "-- both ignored"')
out.append('st')
out.append('rm .gitignore u.txt')
out.append('echo "== untracked directory counts once"')
out.append('mkdir -p nd/deeper; printf \'1\\n\' > nd/one.txt; printf \'2\\n\' > nd/deeper/two.txt')
out.append('st')
out.append('rm -rf nd')
out.append('echo "== staged add"')
emit('"$d/r/.git/index"', index([(0o100644, a_sha, 0, "a.txt", len(a_body)),
                                 (0o100644, b_sha, 0, "b.txt", len(b_body)),
                                 (0o100644, c_sha, 0, "c.txt", len(c_body))]))
out.append("printf 'charlie\\n' > c.txt")
out.append('st')
out.append('echo "== staged delete"')
emit('"$d/r/.git/index"', index([(0o100644, a_sha, 0, "a.txt", len(a_body))]))
out.append('rm c.txt')
out.append('st')
out.append('echo "== a moved file is one rename, not an add and a delete"')
emit('"$d/r/.git/index"', index([(0o100644, a_sha, 0, "a.txt", len(a_body)),
                                 (0o100644, b_sha, 0, "moved.txt", len(b_body))]))
out.append('rm -f b.txt; printf \'bravo\\n\' > moved.txt')
out.append('st')
out.append('rm -f moved.txt; printf \'bravo\\n\' > b.txt')
out.append('echo "== conflicted path is not also counted as staged"')
emit('"$d/r/.git/index"', index([(0o100644, a_sha, 0, "a.txt", len(a_body)),
                                 (0o100644, a_sha, 1, "b.txt", len(b_body)),
                                 (0o100644, b_sha, 2, "b.txt", len(b_body)),
                                 (0o100644, c_sha, 3, "b.txt", len(b_body))]))
out.append('st')
out.append('echo "== unborn branch: everything in the index is staged"')
emit('"$d/r/.git/index"', index([(0o100644, a_sha, 0, "a.txt", len(a_body)),
                                 (0o100644, b_sha, 0, "b.txt", len(b_body))]))
out.append("printf 'ref: refs/heads/fresh\\n' > \"$d/r/.git/HEAD\"")
out.append('st')
out.append("printf 'ref: refs/heads/main\\n' > \"$d/r/.git/HEAD\"")
out.append('echo "== ahead and behind"')
out.append("printf '%s\\n' > \"$d/r/.git/refs/remotes/origin/main\"" % r1)
out.append("printf '[branch \"main\"]\\n\\tremote = origin\\n\\tmerge = refs/heads/main\\n' > \"$d/r/.git/config\"")
out.append('st')
out.append('echo "== upstream found through packed-refs"')
out.append('rm "$d/r/.git/refs/remotes/origin/main"')
out.append("printf '# pack-refs with: peeled fully-peeled sorted \\n%s refs/remotes/origin/main\\n' > \"$d/r/.git/packed-refs\"" % r1)
out.append('st')
out.append('echo "== the rendered segment"')
out.append("PROMPT[format]='<$git>'")
out.append('PROMPT[git][style]=none')
out.append('PROMPT[git][status_style]=none')
out.append('PROMPT[git][ahead_behind_style]=none')
for k in ("staged", "modified", "deleted", "untracked", "conflicted",
          "renamed", "stash", "ahead", "behind"):
    out.append('PROMPT[git][%s_style]=none' % k)
out.append('printf \'changed\\n\' > a.txt; printf \'x\\n\' > u.txt')
out.append('p := prompt render')
out.append('echo "$p"')
out.append('echo "== each count carries its own colour"')
for k in ("staged", "modified", "deleted", "untracked", "conflicted",
          "renamed", "stash", "ahead", "behind"):
    out.append('unset PROMPT[git][%s_style]' % k)
out.append('p := prompt render')
out.append('str replace "$p" $\'\\e\' "<E>" o')
out.append('echo "[$o]"')
out.append('cd /')
out.append('rm -rf "$d"')

open("/home/osakka/hibr/tests/420-status.t", "w").write("\n".join(out) + "\n")
print("wrote tests/420-status.t (%d lines)" % (len(out) + 1))
