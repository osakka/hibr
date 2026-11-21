# Zip and disc images in the archive module (Gitea #488): what each is is
# decided from its bytes, never its name, and each is read through the same
# open, ls, stat, cat and extract as a tar, under the same rules -- every
# name checked before anything is written, nothing landing outside, no link
# out. The zip and the ISO 9660 images are bsdtar's; the raw sectors and the
# CUE sheet are wrapped around its image here, since nothing installed
# writes them. Loaded from HIBR_TESTMODS when that is set, so the sanitizer
# gate's leak pass reads these formats with its own build of the module.
mod load "${HIBR_TESTMODS:-./build/mods}/archive.so"
export TZ=UTC
t=$(mktemp -d)
cd "$t" || exit 1
mkdir -p src/sub
printf 'hello\n' > src/a.txt
seq 1 2000 > src/sub/n.txt
ln -s a.txt src/link
chmod 640 src/a.txt
touch -h -d @1700000000 src/a.txt src/sub/n.txt src/link src/sub
cd src || exit 1
bsdtar --format zip -cf ../b.zip a.txt sub link
bsdtar --format iso9660 -cf ../b.iso a.txt sub link
bsdtar --format iso9660 --options '!rockridge' -cf ../j.iso a.txt sub
cd .. || exit 1
python3 -I - <<'E'
import zipfile
d = open("b.iso", "rb").read()
raw = bytearray()
for i in range(0, len(d), 2048):
    s = b"\0" + b"\xff" * 10 + b"\0" + bytes([0, 2, 0, 1]) + d[i:i + 2048]
    raw += s + bytes(2352 - len(s))
open("GAME.BIN", "wb").write(bytes(raw))
open("game.cue", "w").write('FILE "game.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n')
with zipfile.ZipFile("up.zip", "w") as z:
    z.writestr("ok.txt", "fine")
    z.writestr("../up.txt", "out")
E
for f in b.zip b.iso j.iso game.cue; do
	echo "--- $f"
	archive open a "$f"
	echo "open $?"
	archive ls a
	archive ls a sub
	archive stat a sub/n.txt
	archive cat a sub/n.txt | tail -1
	read -r l < /dev/archive/a/a.txt
	echo "read: $l"
	mkdir "x-$f"
	n := archive extract a "x-$f"
	echo "extracted $n: $?"
	diff -r --no-dereference src "x-$f" && echo "the same tree"
	archive close a
done
echo "--- a zip that climbs out"
mkdir box
archive open u up.zip
archive extract u box
echo "extract $?"
ls -A box
echo "--- the same bytes under any name"
cp b.zip disc.iso
archive open n disc.iso
archive stat n a.txt
cd / && rm -rf "$t"
