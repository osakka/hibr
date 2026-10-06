# img: a 2x2 PNG fixture (red, green / blue, yellow, one pixel each)
# and two JPEGs
# decoded and rendered at exactly its own size, so every cell maps to one
# source pixel with no averaging to make the numbers fuzzy. Recorded
# rather than compared against bash, since there is no reference decoder
# to diff against here -- what matters is that the RGB triples in the
# escapes are exactly the fixture's own known colours.
mod load ./build/mods/img.so

img -w 2 -h 1 tests/img-2x2.png
echo "rc=$?"
img -g -w 2 -h 1 tests/img-2x2.png
echo "rc=$?"

img tests/does-not-exist.png 2>&1
echo "rc=$?"

# A JPEG: four flat quadrants, red, green / blue, yellow, each 16x16, so
# a 2x1 render averages whole quadrants -- near, not exactly, the pure
# colours, JPEG being lossy -- and the size is the image's own.
img size tests/img-quad.jpg
img -w 2 -h 1 tests/img-quad.jpg
echo "rc=$?"

# A wide JPEG, red left and blue right, whose EXIF says it was taken on
# its side (orientation 6): decoded upright, it is tall, red on top.
img size tests/img-rot6.jpg
img -w 1 -h 1 tests/img-rot6.jpg
echo "rc=$?"

# What a file is comes from its first bytes, not its name.
img tests/830-img.t 2>&1
echo "rc=$?"

img 2>&1
echo "rc=$?"

# -o ppm writes the picture at the size it would be drawn, as a binary PPM
# of a cell's two halves; and a PPM reads back, through no library, to the
# same colours -- what lets the login screen draw a person's picture
# without root ever decoding their file
p=/tmp/hibr-img-$$.ppm
img -o ppm -w 2 -h 1 tests/img-2x2.png > "$p"; echo "ppm: $?"
head -c 11 "$p" | od -An -c | tr -s ' '
[ "$(img -w 2 -h 1 tests/img-2x2.png)" = "$(img -w 2 -h 1 "$p")" ] && echo "reads back the same"
printf 'P6\n2 2\n255\nab' > "$p"; img -w 2 -h 1 "$p" 2>&1 | sed 's|/tmp/[^:]*|FILE|'
printf 'P6\n99999 2\n255\n' > "$p"; img -w 2 -h 1 "$p" 2>&1 | sed 's|/tmp/[^:]*|FILE|'
printf 'P6\n2 2\n65535\n' > "$p"; img -w 2 -h 1 "$p" 2>&1 | sed 's|/tmp/[^:]*|FILE|'
img -o gif tests/img-2x2.png 2>&1; echo "bad -o: $?"
rm -f "$p"

# img size reads the header rather than decoding: the desktop's wallpaper
# asks on every change of screen size, and a full decode of a 3840x2160
# photograph to answer with two integers took 206 ms (Gitea #105). A JPEG's
# own EXIF orientation is applied, so this and `img draw` agree about a
# rotated one.
echo "--- size from the header"
img size tests/img-wide.png
img size tests/img-2x2.png
img size tests/img-quad.jpg
img size tests/img-rot6.jpg
img size /dev/null 2>&1 | sed 's/.*: //'
