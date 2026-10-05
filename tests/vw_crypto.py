#!/usr/bin/env python3
"""Check the vw module's crypto against Bitwarden's formats, made independently.

    python3 tests/vw_crypto.py [path-to-hibr]

An account is built here the way Bitwarden's clients build one -- the
master key by PBKDF2-SHA256 (and by Argon2id through libargon2, when it
is there) over the lowercased email, stretched by HKDF-Expand into an
encryption and a MAC key, the user key wrapped as a type-2 EncString
(AES-256-CBC, HMAC-SHA256), items encrypted with it or with a key of
their own -- using Python's cryptography package, not the module. vwk
must give the same master password hash, open the user key, read the
items, refuse a wrong password or a tampered field, wrap and open the key
with a PIN and through a session, and make RFC 6238's TOTP codes.
"""
import base64, ctypes, ctypes.util, hashlib, hmac, os, subprocess, sys, urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import check, report, tree

try:
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.primitives import padding
except ImportError:
    print("vw_crypto: no cryptography package here, nothing compared")
    print("0 passed, 0 failed")
    sys.exit(0)

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
MODS = os.environ.get("HIBR_TESTMODS") or tree("build/mods")
EMAIL, PW, PIN, IT = "Pat.Doe@Example.com ", "correct horse battery staple", "4321", 5000


def stretch(k):
    return (hmac.new(k, b"enc\x01", hashlib.sha256).digest() +
            hmac.new(k, b"mac\x01", hashlib.sha256).digest())


def enc(data, key):
    iv = os.urandom(16)
    p = padding.PKCS7(128).padder()
    e = Cipher(algorithms.AES(key[:32]), modes.CBC(iv)).encryptor()
    ct = e.update(p.update(data) + p.finalize()) + e.finalize()
    mac = hmac.new(key[32:], iv + ct, hashlib.sha256).digest()
    b = lambda x: base64.b64encode(x).decode()
    return "2.%s|%s|%s" % (b(iv), b(ct), b(mac))


def vwk(script, stdin=""):
    r = subprocess.run([sx.HIBR, "-c", "mod load %s/vw.so\n%s" % (MODS, script)], input=stdin,
                       capture_output=True, text=True)
    return r.stdout.strip(), r.returncode, r.stderr


salt = EMAIL.strip().lower().encode()
mk = hashlib.pbkdf2_hmac("sha256", PW.encode(), salt, IT, 32)
mph = base64.b64encode(hashlib.pbkdf2_hmac("sha256", mk, PW.encode(), 1, 32)).decode()
uk = os.urandom(64)
protected = enc(uk, stretch(mk))
item = enc(b"hunter2 \xc3\xa9", uk)
ik = os.urandom(64)
itemkey = enc(ik, uk)
item2 = enc(b"behind its own key", ik)
KDF = "-e '%s' -k pbkdf2 -i %d" % (EMAIL, IT)

out, rc, err = vwk("vwk hash %s -s" % KDF, PW + "\n")
check("the master password hash is the one Bitwarden's clients send", out == mph and rc == 0, (out, mph, err))
out, rc, err = vwk("vwk hash %s -s > /dev/null; vwk open '%s' && vwk state; vwk dec '%s'; vwk dec -k '%s' '%s'"
                   % (KDF, protected, item, itemkey, item2), PW + "\n")
check("it opens the account's key and reads an item, and one behind a key of its own",
      out.split("\n") == ["unlocked", "hunter2 é", "behind its own key"], (out, err))
out, rc, err = vwk("vwk hash %s -s > /dev/null; vwk open '%s'; echo $?; vwk state" % (KDF, protected), "wrong\n")
check("a wrong master password opens nothing", out.split("\n") == ["1", "locked"], (out, err))
bad = item[:-6] + ("AAAA" if item[-6:-2] != "AAAA" else "BBBB") + item[-2:]
out, rc, err = vwk("vwk hash %s -s > /dev/null; vwk open '%s'; vwk dec '%s'; echo $?" % (KDF, protected, bad), PW + "\n")
check("a field whose MAC does not hold is refused, not decrypted", out == "1", (out, err))
out, rc, err = vwk("vwk hash %s -s > /dev/null; vwk open '%s'; w := vwk pin wrap %s -s; vwk lock; vwk state;"
                   " vwk pin open %s -s \"$w\" <<< '%s'; vwk state; vwk dec '%s'"
                   % (KDF, protected, KDF, KDF, PIN, item), PW + "\n" + PIN + "\n")
check("a PIN wraps the key and opens it again after a lock",
      out.split("\n") == ["locked", "unlocked", "hunter2 é"], (out, err))
out, rc, err = vwk("vwk hash %s -s > /dev/null; vwk open '%s'; w := vwk pin wrap %s -s; vwk lock;"
                   " vwk pin open %s -s \"$w\" <<< 9999; echo $?; vwk state"
                   % (KDF, protected, KDF, KDF), PW + "\n" + PIN + "\n")
check("and a wrong PIN does not", out.split("\n") == ["1", "locked"], (out, err))
out, rc, err = vwk("vwk hash %s -s > /dev/null; vwk open '%s'; s := vwk session new; vwk lock;"
                   " IFS=$'\\t' read -r k w <<< \"$s\"; vwk session open \"$k\" \"$w\"; vwk dec '%s'"
                   % (KDF, protected, item), PW + "\n")
check("a session key hands the vault to the next command, as BW_SESSION does", out == "hunter2 é", (out, err))
out, rc, err = vwk("vwk hash %s -s > /dev/null; vwk open '%s'; vwk idle 1; sleep 2; vwk state" % (KDF, protected), PW + "\n")
check("an idle vault locks itself", out == "locked", (out, err))
# RFC 6238's SHA-1 vectors, the 20-byte ASCII secret in base32.
sec = base64.b32encode(b"12345678901234567890").decode()
want = {59: "94287082", 1111111109: "07081804", 1234567890: "89005924", 2000000000: "69279037"}
got = {t: vwk("vwk totp 'otpauth://totp/x?secret=%s&digits=8' %d" % (sec, t))[0] for t in want}
check("TOTP codes are RFC 6238's", got == want, got)
form = "pat+é@x.com a/b~c"
out, rc, err = vwk("vwk form '%s'" % form)
check("a form value is percent-encoded byte by byte, as urllib does it",
      out == urllib.parse.quote(form, safe="~"), (out, err))

lib = ctypes.util.find_library("argon2")
if lib:
    a2 = ctypes.CDLL(lib)
    a2.argon2id_hash_raw.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_char_p,
                                     ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p,
                                     ctypes.c_size_t]
    raw = ctypes.create_string_buffer(32)
    s2 = hashlib.sha256(salt).digest()
    if a2.argon2id_hash_raw(3, 64 * 1024, 4, PW.encode(), len(PW), s2, 32, raw, 32) != 0:
        sys.exit("libargon2 would not hash here")
    amk = raw.raw
    amph = base64.b64encode(hashlib.pbkdf2_hmac("sha256", amk, PW.encode(), 1, 32)).decode()
    aprot = enc(uk, stretch(amk))
    out, rc, err = vwk("vwk hash -e '%s' -k argon2 -i 3 -m 64 -p 4 -s; vwk open '%s' && vwk dec '%s'"
                       % (EMAIL, aprot, item), PW + "\n")
    check("an Argon2id account hashes and opens the same way", out.split("\n") == [amph, "hunter2 é"], (out, err))
else:
    check("an Argon2id account hashes and opens the same way (no libargon2 here: skipped)", True)

report(11)
