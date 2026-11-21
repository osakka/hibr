#!/usr/bin/env python3
"""Write mods/htmlent.c from WHATWG's entities.json:
    curl -sO https://html.spec.whatwg.org/entities.json
    python3 tools/htmlents.py entities.json > mods/htmlent.c
The HTML5 named character references, compiled into both the html and the
md module (Gitea #355). Each name once, without & and ;, sorted byte by
byte for a binary search, in one blob of name NUL characters NUL; an
offset per name, with bit 15 saying whether it is also recognised without
its semicolon (the legacy ones). A blob and offsets need no relocation,
where a table of pointers needed two per entry."""
import json, sys

FLAG = 0x8000

d = json.load(open(sys.argv[1]))
names = {}
for k, v in d.items():
    nm = k[1:]
    semi = nm.endswith(";")
    nm = nm.rstrip(";")
    e = names.setdefault(nm, [v["characters"], 0])
    if not semi:
        e[1] = 1


def c(b):
    return "".join("\\%03o" % x for x in b)


rows = sorted(names.items(), key=lambda kv: kv[0].encode())
blob, offs = [], []
at = 0
for nm, (ch, leg) in rows:
    n, v = nm.encode(), ch.encode("utf-8")
    if any(x < 0x21 or x > 0x7e or x in b'"\\?' for x in n) or 0 in v:
        sys.exit("htmlents: an entry this file cannot hold as it is: %r" % nm)
    offs.append(at | (FLAG if leg else 0))
    blob.append('\t"%s\\000" "%s\\000"' % (nm, c(v)))
    at += len(n) + 1 + len(v) + 1
if at > FLAG:
    sys.exit("htmlents: the blob is %d bytes, past the %d an offset can name" % (at, FLAG))

print('#include "htmlent.h"')
print('#include <string.h>\n')
print("const char he_blob[] =")
print("\n".join(blob) + ";\n")
print("const unsigned short he_off[] = {")
for i in range(0, len(offs), 8):
    print("\t" + " ".join("0x%04x," % o for o in offs[i:i + 8]))
print("};\n")
print("const size_t he_n = sizeof he_off / sizeof he_off[0];\n")
print("""/* The characters a named reference stands for, by its exact name, or 0. */
const char *he_find(const char *nm, size_t len, int *legacy)
{
\tsize_t lo = 0, hi = he_n, mid, k;
\tconst char *e;
\tint c;

\twhile (lo < hi) {
\t\tmid = (lo + hi) / 2;
\t\te = he_blob + (he_off[mid] & 0x7fff);
\t\tk = strlen(e);
\t\tc = memcmp(e, nm, k < len ? k : len);
\t\tif (!c)
\t\t\tc = (k > len) - (k < len);
\t\tif (c < 0)
\t\t\tlo = mid + 1;
\t\telse if (c > 0)
\t\t\thi = mid;
\t\telse {
\t\t\tif (legacy)
\t\t\t\t*legacy = !!(he_off[mid] & 0x8000);
\t\t\treturn e + k + 1;
\t\t}
\t}
\treturn 0;
}""")
