json parse doc '{"name":"hibr","version":0.12,"tags":["shell","tiny"],"nested":{"ok":true,"none":null},"count":3}'
echo "name=$(json get doc .name)"
json get doc .version v
echo "version=$v type=$(json type doc .version)"
echo "tag0=$(json get doc .tags[0]) tag1=$(json get doc .tags[1])"
echo "bool=$(json get doc .nested.ok) type=$(json type doc .nested.ok)"
echo "null type=$(json type doc .nested.none)"
echo "keys=$(json keys doc .)"
echo "len=$(json len doc .tags)"
echo "subscript=${doc[tags][1]} ${doc[nested][ok]}"
json set doc .version 0.13
json set doc .nested.added "hello world"
json set doc .count 42
json set doc .quoted 7 -s
echo "types after set: $(json type doc .count) $(json type doc .quoted)"
json emit doc
json emit doc -p
json parse empty '{}'
json emit empty
json parse arr '[1,2,[3,4]]'
echo "nested array: $(json get arr .[2][1]) len=$(json len arr .)"
json emit arr
json parse esc '{"s":"line\nbreak\ttab \"quoted\""}'
json emit esc
json parse bad '{oops' 2>/dev/null
echo "bad status=$?"
