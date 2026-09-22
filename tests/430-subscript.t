n=content-type

h["a-b"]=one
h[plain]=two
echo "quoted write: ${!h[*]}"
echo "quoted read: ${h["a-b"]}"

g["$n"]=json
echo "expanded write: ${!g[*]}"
echo "expanded read: ${g["$n"]}"

m[content-type]=folded
echo "unquoted stays arithmetic: ${!m[*]}"

unset h["a-b"]
echo "quoted unset: ${!h[*]}"
unset g["$n"]
echo "expanded unset: ${!g[*]}"

k[x]=1
k[y]=2
unset k[x]
echo "plain unset: ${!k[*]}"

a=(p q r)
a[7]=s
unset a[1]
echo "digit unset: ${!a[*]}"
echo "arithmetic subscript: ${a[3+4]}"

i=2
echo "bare name subscript: ${a[i]}"

lst=('w[a-b]')
w["a-b"]=kept
unset "${lst[@]}"
echo "quoted expansion unset: ${!w[*]}"

json parse j '{"content-type":"app/json"}'
echo "json key addressable: ${j["content-type"]}"

d["a-b"]=x
d["a-b"]+=y
echo "append: ${d["a-b"]}"
echo "default on missing: ${d["no-key"]:-fallback}"

ev["x-y"]=1
ev[k]=2
eval 'unset ev["x-y"]'
echo "unset via eval: ${!ev[*]}"

vr["x-y"]=1
vr[k]=2
c=unset
$c vr["x-y"]
echo "unset via variable: ${!vr[*]}"

cm["x-y"]=1
cm[k]=2
command unset cm["x-y"]
echo "unset via command: ${!cm[*]}"

alias u=unset

ax=(p q r)
n=1
u ax[n]
echo "arithmetic survives an alias: ${!ax[*]}"

al["x-y"]=1
al[k]=2
u al["x-y"]
echo "quoting survives an alias: ${!al[*]}"

rd=()
read -r rd[0] rd[1] <<< "one two"
echo "read into elements: ${rd[0]}/${rd[1]}"
rq["a-b"]=start
read -r rq["a-b"] <<< "changed"
echo "read into a quoted key: ${rq["a-b"]}"

vt[k]=v
[[ -v vt[k] ]] && echo "test -v sees an element"
[[ -v vt[nope] ]] || echo "test -v misses an absent one"
va=(x y z)
unset va[1]
[[ -v va[1] ]] || echo "test -v follows unset"
[[ -v va[-1] ]] && echo "test -v takes a negative subscript"
vq["a-b"]=1
[[ -v vq["a-b"] ]] && echo "test -v takes a quoted subscript"
vn=(x y z)
vi=2
[[ -v vn[vi] ]] && echo "test -v takes a name subscript"

export ex[k]=v 2>/dev/null
echo "export refuses a subscript: $?"
