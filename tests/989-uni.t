# uni: text for a cell grid -- display order, Arabic shaping, widths and
# classes, from one Unicode version (#68). Recorded: the module is hibr's
# own. The algorithm itself is checked against Unicode's conformance
# suites by tests/uni_bidi.py and against FriBidi by tests/uni_shape.py.

mod load ./build/mods/uni.so || exit 1
uni version
uni vis "abc"
uni vis "سلام"
uni vis "hello مرحبا world"
uni vis "العدد 123 هنا"
uni vis -d rtl "a b (c)"
uni vis -d ltr "(سلام)"
uni vis "لا إله"
uni shape "بلا"
uni width "abc"
uni width "漢字"
uni width "بِسْمِ"
uni class "a ب 1 ١ ("
x := uni vis "عربي"; echo "bound: $x"
uni 2> /dev/null; echo "usage: $?"
uni vis 2> /dev/null; echo "no text: $?"
