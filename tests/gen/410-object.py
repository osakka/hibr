import zlib, hashlib, struct, binascii

def oct_escape(b):
    return "".join("\\0%03o" % c for c in b)

def emit(path, data, out):
    """nsh lines writing exact bytes to path"""
    e = oct_escape(data)
    chunks = [e[i:i+320] for i in range(0, len(e), 320)]
    out.append("printf '%s' > %s" % (chunks[0], path))
    for c in chunks[1:]:
        out.append("printf '%s' >> %s" % (c, path))

def mkobj(typ, body):
    raw = typ.encode() + b" " + str(len(body)).encode() + b"\0" + body
    return hashlib.sha1(raw).hexdigest(), raw

def varint_size(t, n):
    b = bytearray()
    c = (t << 4) | (n & 15)
    n >>= 4
    while n:
        b.append(c | 0x80); c = n & 0x7f; n >>= 7
    b.append(c)
    return bytes(b)

def dvarint(n):
    b = bytearray()
    while True:
        c = n & 0x7f; n >>= 7
        if n: b.append(c | 0x80)
        else: b.append(c); break
    return bytes(b)

def copy_insn(off, size):
    b = bytearray([0]); flags = 0
    for i in range(4):
        v = (off >> (8 * i)) & 0xff
        if v: b.append(v); flags |= 1 << i
    for i in range(3):
        v = (size >> (8 * i)) & 0xff
        if v: b.append(v); flags |= 0x10 << i
    b[0] = 0x80 | flags
    return bytes(b)

def make_delta(base, result):
    """copy a prefix of base, then insert the rest literally"""
    d = dvarint(len(base)) + dvarint(len(result))
    keep = len(base) // 2
    assert result[:keep] == base[:keep] and 0 < keep < 0x10000
    d += copy_insn(0, keep)
    tail = result[keep:]
    while tail:
        part, tail = tail[:127], tail[127:]
        d += bytes([len(part)]) + part
    return d

out = []
out.append("mod load ./mods/prompt.so")
out.append('d=/tmp/nsh-obj-$$')
out.append('rm -rf "$d"')
out.append('mkdir -p "$d/repo/.git/objects/pack" "$d/repo/.git/refs/heads"')
out.append("printf 'ref: refs/heads/main\\n' > \"$d/repo/.git/HEAD\"")

# ---- loose objects, three deflate block types ----
cases = []
b1 = b"hello world\n"
sha1, raw1 = mkobj("blob", b1)
co = zlib.compressobj(0)                      # stored blocks
z1 = co.compress(raw1) + co.flush()
cases.append(("stored", sha1, z1, "blob", len(b1)))

b2 = b"".join(b"line %d of a repetitive file\n" % i for i in range(200))
sha2, raw2 = mkobj("blob", b2)
z2 = zlib.compress(raw2, 9)                   # dynamic huffman
cases.append(("dynamic", sha2, z2, "blob", len(b2)))

b3 = b"abcabcabc\n"
sha3, raw3 = mkobj("blob", b3)
co = zlib.compressobj(9, zlib.DEFLATED, 15, 9, zlib.Z_FIXED)
z3 = co.compress(raw3) + co.flush()           # fixed huffman
cases.append(("fixed", sha3, z3, "blob", len(b3)))

tree_body = b"100644 hello\0" + bytes.fromhex(sha1)
sha4, raw4 = mkobj("tree", tree_body)
cases.append(("tree", sha4, zlib.compress(raw4, 6), "tree", len(tree_body)))

commit_body = (b"tree " + sha4.encode() + b"\n"
               b"author A <a@e> 1700000000 +0000\n"
               b"committer A <a@e> 1700000000 +0000\n\nmsg\n")
sha5, raw5 = mkobj("commit", commit_body)
cases.append(("commit", sha5, zlib.compress(raw5, 6), "commit", len(commit_body)))

for name, sha, z, typ, sz in cases:
    out.append('mkdir -p "$d/repo/.git/objects/%s"' % sha[:2])
    emit('"$d/repo/.git/objects/%s/%s"' % (sha[:2], sha[2:]), z, out)

# ---- a pack: base blob, OFS_DELTA on it, REF_DELTA on a loose object ----
pbase = b"".join(b"packed base line %d\n" % i for i in range(40))
psha, praw = mkobj("blob", pbase)
pres = pbase[:len(pbase)//2] + b"DELTA TAIL ofs\n"
dsha, draw = mkobj("blob", pres)
delta_ofs = make_delta(pbase, pres)

rres = b1[:len(b1)//2] + b"DELTA TAIL ref\n"
rsha, rraw = mkobj("blob", rres)
delta_ref = make_delta(b1, rres)

body = b""
offs = {}
offs[psha] = 12 + len(body)
body += varint_size(3, len(pbase)) + zlib.compress(pbase, 6)
offs[dsha] = 12 + len(body)
body += varint_size(6, len(delta_ofs)) 
rel = offs[dsha] - offs[psha]
ob = bytearray(); ob.append(rel & 0x7f); rel >>= 7
while rel:
    rel -= 1
    ob.append(0x80 | (rel & 0x7f)); rel >>= 7
body += bytes(reversed(ob)) + zlib.compress(delta_ofs, 6)
offs[rsha] = 12 + len(body)
body += varint_size(7, len(delta_ref)) + bytes.fromhex(sha1) + zlib.compress(delta_ref, 6)

pack = b"PACK" + struct.pack(">II", 2, 3) + body
pack += hashlib.sha1(pack).digest()

shas = sorted([psha, dsha, rsha])
fan = []
for i in range(256):
    fan.append(sum(1 for s in shas if int(s[:2], 16) <= i))
idx = b"\377tOc" + struct.pack(">I", 2)
idx += b"".join(struct.pack(">I", c) for c in fan)
idx += b"".join(bytes.fromhex(s) for s in shas)
idx += b"".join(struct.pack(">I", 0) for s in shas)
idx += b"".join(struct.pack(">I", offs[s]) for s in shas)
idx += pack[-20:]
idx += hashlib.sha1(idx).digest()

emit('"$d/repo/.git/objects/pack/pack-test.pack"', pack, out)
emit('"$d/repo/.git/objects/pack/pack-test.idx"', idx, out)

# ---- a second pack carrying a version 1 index ----
v1base = b"".join(b"v1 base line %d\n" % i for i in range(30))
v1sha, _ = mkobj("blob", v1base)
v1res = v1base[:len(v1base) // 2] + b"V1 DELTA TAIL\n"
v1dsha, _ = mkobj("blob", v1res)
v1delta = make_delta(v1base, v1res)

body2 = b""
offs2 = {}
offs2[v1sha] = 12 + len(body2)
body2 += varint_size(3, len(v1base)) + zlib.compress(v1base, 6)
offs2[v1dsha] = 12 + len(body2)
body2 += varint_size(7, len(v1delta)) + bytes.fromhex(v1sha)
body2 += zlib.compress(v1delta, 6)
pack2 = b"PACK" + struct.pack(">II", 2, 2) + body2
pack2 += hashlib.sha1(pack2).digest()

shas2 = sorted([v1sha, v1dsha])
fan2 = [sum(1 for x in shas2 if int(x[:2], 16) <= i) for i in range(256)]
idx2 = b"".join(struct.pack(">I", c) for c in fan2)
for x in shas2:
    idx2 += struct.pack(">I", offs2[x]) + bytes.fromhex(x)
idx2 += pack2[-20:]
idx2 += hashlib.sha1(idx2).digest()

emit('"$d/repo/.git/objects/pack/pack-v1.pack"', pack2, out)
emit('"$d/repo/.git/objects/pack/pack-v1.idx"', idx2, out)

out.append('cd "$d/repo"')
out.append('echo "== loose objects, all three deflate block types"')
for name, sha, z, typ, sz in cases:
    out.append('printf "%-8s " ; prompt object %s' % (name, sha))
out.append('echo "== loose content"')
out.append('prompt object -p %s' % sha1)
out.append('prompt object -p %s' % sha3)
out.append('echo "== tree content, binary sha rendered as text is not printed"')
out.append('n := prompt object %s' % sha4)
out.append('echo "tree=$n"')
out.append('echo "== packed base, ofs delta, ref delta"')
out.append('printf "base     " ; prompt object %s' % psha)
out.append('printf "ofs      " ; prompt object %s' % dsha)
out.append('printf "ref      " ; prompt object %s' % rsha)
out.append('echo "== delta reconstruction is exact"')
out.append('prompt object -p %s' % dsha + ' | tail -2')
out.append('prompt object -p %s' % rsha)
out.append('echo "== version 1 pack index"')
out.append('printf "v1 base  " ; prompt object %s' % v1sha)
out.append('printf "v1 delta " ; prompt object %s' % v1dsha)
out.append('prompt object -p %s' % v1dsha + ' | tail -1')
out.append('echo "== missing object"')
out.append('prompt object 0000000000000000000000000000000000000000 2>"$d/e"; echo "status=$?"; cat "$d/e"')
out.append('echo "== malformed argument"')
out.append('prompt object zz 2>"$d/e"; echo "status=$?"; cat "$d/e"')
out.append('prompt object 00112233445566778899aabbccddeeff0011223 2>/dev/null; echo "status=$?"')
out.append('echo "== corrupt loose object is refused, not fatal"')
out.append('printf \'\\0170\\0001garbagegarbage\' > "$d/repo/.git/objects/%s/%s"' % (sha3[:2], sha3[2:]))
out.append('prompt object %s 2>/dev/null; echo "status=$?"' % sha3)
out.append('echo "== truncated pack index is ignored"')
out.append('printf \'\\0377tOc\\0000\\0000\\0000\\0002\' > "$d/repo/.git/objects/pack/pack-test.idx"')
out.append('prompt object %s 2>/dev/null; echo "status=$?"' % psha)
out.append('echo "== the prompt still renders"')
out.append("PROMPT[format]='<$git>'")
out.append('PROMPT[git][style]=none')
out.append('p := prompt render')
out.append('echo "prompt=$p"')
out.append('cd /')
out.append('rm -rf "$d"')

open("/home/osakka/hibr/tests/410-object.t","w").write("\n".join(out) + "\n")
print("wrote tests/410-object.t  (%d lines)" % (len(out)+1))
