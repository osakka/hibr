#!/usr/bin/env python3
"""A stand-in Bitwarden / Vaultwarden server for tests/vw.py.

    python3 tests/bwserve.py [--kdf pbkdf2|argon2] [--2fa 0|1|3]

Prints "port N" and serves, on 127.0.0.1, the few endpoints a client
needs to log in and sync -- /identity/accounts/prelogin,
/identity/connect/token (the password and refresh-token grants) and
/api/sync -- for one account, pat@example.com with the password "correct
horse battery staple". Its vault is encrypted the way Bitwarden's
clients encrypt one, with Python's cryptography package, not with hibr:
two logins (one with a TOTP secret, one behind a key of its own), a
secure note and a folder. GET /stats says how many token and sync
requests it has had, so a test can tell a sync from a cached read.

With --2fa it demands two-factor for that provider number, and every
string and status below is Vaultwarden's own rather than a plausible
one -- a stand-in written from the same memory as the client would
agree with the client and prove nothing. Read from src/api/identity.rs
and src/db/models/two_factor.rs:

  * wanted:       400, {"error":"invalid_grant",
                        "error_description":"Two factor required.",
                        "TwoFactorProviders":["0"],
                        "TwoFactorProviders2":{"0":null}}
  * a wrong code: 401, "Two-step token is invalid. Try again."
  * remembered:   the successful reply carries "TwoFactorToken",
                  capitalised, when two_factor_remember was 1
  * providers:    0 authenticator, 1 email, 2 Duo, 3 YubiKey, 4 U2f,
                  5 remember, 6 organisation Duo, 7 WebAuthn,
                  8 recovery code
  * the fields:   two_factor_token / two_factor_provider /
                  two_factor_remember, each with an uncased alias
                  without the underscores -- so the official clients'
                  twoFactorToken spelling is accepted too, and this
                  takes either rather than only the one hibr sends.
"""
import base64, hashlib, hmac, json, os, sys, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import padding

EMAIL, PW = "pat@example.com", "correct horse battery staple"
IT = 5000
KDF = "argon2" if "--kdf" in sys.argv and sys.argv[sys.argv.index("--kdf") + 1] == "argon2" else "pbkdf2"
STATS = {"token": 0, "refresh": 0, "sync": 0, "twofa": 0, "mailed": 0}
TFA = (int(sys.argv[sys.argv.index("--2fa") + 1]) if "--2fa" in sys.argv else None)
CODE = "123456"            # what provider 0, 1 and 3 accept
RECOVERY = "0123456789abcdef0123456789abcdef"
REMEMBER = "remember-pat"  # what a client that asked to be remembered gets


def formget(f, *names):
    """A form field under any of the names the server accepts for it."""
    for n in names:
        for k in f:
            if k.lower() == n:
                return f[k][0]
    return ""


def stretch(k):
    return (hmac.new(k, b"enc\x01", hashlib.sha256).digest() +
            hmac.new(k, b"mac\x01", hashlib.sha256).digest())


def enc(data, key):
    if isinstance(data, str):
        data = data.encode()
    iv = os.urandom(16)
    p = padding.PKCS7(128).padder()
    e = Cipher(algorithms.AES(key[:32]), modes.CBC(iv)).encryptor()
    ct = e.update(p.update(data) + p.finalize()) + e.finalize()
    mac = hmac.new(key[32:], iv + ct, hashlib.sha256).digest()
    b = lambda x: base64.b64encode(x).decode()
    return "2.%s|%s|%s" % (b(iv), b(ct), b(mac))


def master():
    if KDF == "argon2":
        import ctypes, ctypes.util
        a2 = ctypes.CDLL(ctypes.util.find_library("argon2"))
        a2.argon2id_hash_raw.argtypes = [ctypes.c_uint32] * 3 + [ctypes.c_char_p, ctypes.c_size_t,
                                                                 ctypes.c_char_p, ctypes.c_size_t,
                                                                 ctypes.c_void_p, ctypes.c_size_t]
        raw = ctypes.create_string_buffer(32)
        a2.argon2id_hash_raw(3, 64 * 1024, 4, PW.encode(), len(PW), hashlib.sha256(EMAIL.encode()).digest(),
                             32, raw, 32)
        return raw.raw
    return hashlib.pbkdf2_hmac("sha256", PW.encode(), EMAIL.encode(), IT, 32)


MK = master()
HASH = base64.b64encode(hashlib.pbkdf2_hmac("sha256", MK, PW.encode(), 1, 32)).decode()
UK = os.urandom(64)
PROTECTED = enc(UK, stretch(MK))
IK = os.urandom(64)
FOLDER = "f0000000-0000-0000-0000-000000000001"
CIPHERS = [
    {"id": "c0000000-0000-0000-0000-000000000001", "type": 1, "folderId": FOLDER, "key": None,
     "name": enc("Example Mail", UK), "notes": None,
     "login": {"username": enc("pat", UK), "password": enc("s3crét-mail", UK),
               "totp": enc("JBSWY3DPEHPK3PXP", UK), "uris": [{"uri": enc("https://mail.example.com", UK)}]},
     "revisionDate": "2026-10-01T00:00:00Z"},
    {"id": "c0000000-0000-0000-0000-000000000002", "type": 1, "folderId": None, "key": enc(IK, UK),
     "name": enc("Bank", IK), "notes": enc("the one with the card", IK),
     "login": {"username": enc("pat.doe", IK), "password": enc("b4nk-p4ss", IK), "totp": None,
               "uris": [{"uri": enc("https://bank.example.com", IK)}]},
     "revisionDate": "2026-10-02T00:00:00Z"},
    {"id": "c0000000-0000-0000-0000-000000000003", "type": 2, "folderId": FOLDER, "key": None,
     "name": enc("Wifi", UK), "notes": enc("network: home\npassword: h0me-w1fi", UK), "login": None,
     "secureNote": {"type": 0}, "revisionDate": "2026-10-03T00:00:00Z"},
]
TOKENS = {}


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def reply(self, code, obj):
        b = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n).decode() if n else ""

    def do_GET(self):
        if self.path == "/stats":
            return self.reply(200, STATS)
        if self.path.startswith("/api/sync"):
            auth = self.headers.get("Authorization", "")
            if not auth.startswith("Bearer ") or auth[7:] not in TOKENS:
                return self.reply(401, {"message": "Unauthorized"})
            STATS["sync"] += 1
            return self.reply(200, {
                "object": "sync",
                "profile": {"email": EMAIL, "name": "Pat Doe", "key": PROTECTED, "organizations": []},
                "folders": [{"id": FOLDER, "name": enc("Personal", UK)}],
                "collections": [], "ciphers": CIPHERS})
        self.reply(404, {"message": "not found"})

    def do_POST(self):
        b = self.body()
        if self.path == "/identity/accounts/prelogin":
            if KDF == "argon2":
                return self.reply(200, {"kdf": 1, "kdfIterations": 3, "kdfMemory": 64, "kdfParallelism": 4})
            return self.reply(200, {"kdf": 0, "kdfIterations": IT, "kdfMemory": None, "kdfParallelism": None})
        if self.path == "/api/two-factor/send-email-login":
            # Vaultwarden mails the code only when it is asked to; the 400
            # does not send it. Unauthenticated, and it wants the master
            # password hash the client already has.
            STATS["mailed"] += 1
            return self.reply(200, {})
        if self.path == "/identity/connect/token":
            f = urllib.parse.parse_qs(b)
            g = f.get("grant_type", [""])[0]
            if g == "password" and f.get("username", [""])[0].lower() == EMAIL and f.get("password", [""])[0] == HASH:
                STATS["token"] += 1
            elif g == "refresh_token" and f.get("refresh_token", [""])[0] == "refresh-pat":
                STATS["refresh"] += 1
            else:
                # A wrong password: no TwoFactorProviders, which is how a
                # client tells this from "two factor wanted" -- the status
                # is 400 either way.
                return self.reply(400, {"error": "invalid_grant",
                                        "ErrorModel": {"Message": "Username or password is incorrect. Try again"}})
            if TFA is not None and g == "password":
                prov = formget(f, "two_factor_provider", "twofactorprovider")
                tok = formget(f, "two_factor_token", "twofactortoken")
                rem = formget(f, "two_factor_remember", "twofactorremember")
                ok = False
                if prov == "5":
                    ok = tok == REMEMBER
                elif prov == "8":
                    ok = tok == RECOVERY
                elif tok and prov == str(TFA):
                    ok = tok == CODE
                if not ok:
                    if not tok:
                        STATS["twofa"] += 1
                        return self.reply(400, {
                            "error": "invalid_grant",
                            "error_description": "Two factor required.",
                            "TwoFactorProviders": [str(TFA)],
                            "TwoFactorProviders2": {str(TFA): None}})
                    if prov == "5":
                        # A remember token the server no longer honours is
                        # not a wrong code: it asks again, which is what
                        # makes a client fall back rather than lock out.
                        STATS["twofa"] += 1
                        return self.reply(400, {
                            "error": "invalid_grant",
                            "error_description": "Two factor required.",
                            "TwoFactorProviders": [str(TFA)],
                            "TwoFactorProviders2": {str(TFA): None}})
                    return self.reply(401, {
                        "error": "invalid_grant",
                        "error_description": "Two-step token is invalid. Try again.",
                        "ErrorModel": {"Message": "Two-step token is invalid. Try again."}})
            t = "access-%d" % (STATS["token"] + STATS["refresh"])
            TOKENS[t] = 1
            r = {"access_token": t, "expires_in": 3600, "token_type": "Bearer", "refresh_token": "refresh-pat"}
            if g == "password":
                r.update({"Key": PROTECTED, "Kdf": 1 if KDF == "argon2" else 0, "KdfIterations": 3 if KDF == "argon2" else IT})
                if TFA is not None and formget(f, "two_factor_remember", "twofactorremember") == "1":
                    r["TwoFactorToken"] = REMEMBER
            return self.reply(200, r)
        self.reply(404, {"message": "not found"})


srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
print("port %d" % srv.server_address[1], flush=True)
srv.serve_forever()
