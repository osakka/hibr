# Lengths and slices count characters, not bytes -- docs/adr/0021. Recorded,
# because a bash in a C locale counts bytes and this is deliberately not that.

s="─é漢ab"
echo "len ${#s}"
echo "slice [${s:0:3}] [${s:2}] [${s:1:2}] [${s: -2}]"
echo "past [${s:9}] [${s:2:99}] [${s:0:0}]"

e=""
echo "empty ${#e} [${e:0:2}]"
a="plain"
echo "ascii ${#a} [${a:1:3}]"

echo "str len $(str len "$s") width $(str width "$s")"
echo "str slice [$(str slice "$s" 1 2)] [$(str slice "$s" -2)]"
echo "str index $(str index "$s" "漢")"
echo "pad [$(str pad "漢字" 8 .)] [$(str pad "ab" 8 .)] [$(str pad "漢字" -8 .)]"

# The border that found this, drawn the way a script would draw it.
bar=""
while [ ${#bar} -lt 10 ]; do bar="$bar─"; done
bar="${bar:0:10}"
echo "bar [$bar] ${#bar}"

# A slice must never cut a character in half.
i=0
while [ $i -le 5 ]; do
  echo "cut $i [${s:0:$i}]"
  i=$((i + 1))
done
