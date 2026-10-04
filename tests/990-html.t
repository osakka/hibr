# html: the parser through handles -- parse, query, text, attr, tag, kids,
# parent, title -- and lines, the layout a terminal draws: hidden content
# and tracking pixels dropped, tables as grids or linearised, links with
# their columns, images as boxes. The tree-construction conformance suite
# is tests/html_tree.py; this is the module's own surface. Recorded: bash
# has no html.
mod load ./build/mods/html.so

h := html parse -t '<!DOCTYPE html><title>T &amp; t</title><p id=a class="x y">One<p class=y>Two <b>bold</b><ul><li>i1<li>i2</ul>'
echo "handle $h, title [$(html title $h)]"
for n in $(html query $h 'p.y'); do echo "p.y: $(html text $h $n) (tag $(html tag $h $n), parent $(html tag $h $(html parent $h $n)))"; done
n := html query $h '#a'; echo "#a: $(html attr $h $n class)"
html attr $h $n
html query $h 'li:nth-child(2), p + p > b' | while read -r k; do echo "match: $(html text $h $k)"; done
html query $h 'table' || echo "no table: status $?"
html query $h 'p[' 2>&1; echo "bad selector: $?"
echo "kids of body: $(for k in $(html kids $h $(html query $h body)); do printf '%s ' "$(html tag $h $k)"; done)"
html close $h
html text $h 2>&1; echo "closed: $?"

m := html parse -t '<div style="display:none">preheader</div><table role=presentation><tr><td><img src="cid:l" alt=Logo width=80 height=40><td>Order 7</table><h1>Hello, <i>you</i></h1><p>A paragraph long enough that it has to wrap at twenty-four columns, with <a href="https://e.x/t">a link</a> in it.</p><table border=1><tr><th>Item<th>Qty<tr><td>Widget<td>2</table><blockquote>quoted<br>text</blockquote><ol start=3><li>three<li>four</ol><img src=x width=1 height=1><p style="font-size:0px">junk</p><pre>a	b</pre>'
html lines $m 24
echo ---
r := html lines $m 24
i=0; while [ -n "${r[$i]["s"]+x}" ]; do printf '%2d %-24s %s\n' "$i" "${r[$i]["s"]}" "${r[$i]["a"]}"; i=$((i + 1)); done
echo ---
r := html lines $m 24 -i -a
echo "image box: [${r[0]["img"]}]"; echo "rule in ascii: [${r[12]["t"]}]"
html lines $m 2>&1; echo "no width: $?"
html close $m

f := html parse -f tr -t '<td>a<td>b'; html dump -f tr -t '<td>a<td>b'; echo "fragment kids: $(html kids $f $(html query $f html) | wc -l)"
