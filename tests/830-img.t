# img: a 2x2 PNG fixture (red, green / blue, yellow, one pixel each)
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

img tests/img-2x2.jpg 2>&1
echo "rc=$?"

img 2>&1
echo "rc=$?"
