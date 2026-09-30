# strict: checks a file asks for, refused with the file and line, and only
# in that file -- see docs/adr/0023.
d=/tmp/hibr-strict-$$
mkdir -p "$d"
cd "$d"

cat > funcs.hibr <<'X'
strict functions
f() { echo one; }
f() { echo two; }
echo "second definition: status $?"
f
X
"$HIBR" funcs.hibr 2>&1

cat > vars.hibr <<'X'
strict vars
existing=0
g() { newglobal=1; }
g; echo "new global: status $? ${newglobal-unset}"
h() {
	existing=5
	local mine=1
	declare -g made=2
	LC_HIBR_TEST=1 true
	echo "declared ones: $?"
}
h; echo "existing=$existing made=$made"
r() { read -r fromread <<< "text"; }
r; echo "read: status $? ${fromread-unset}"
l() { for loopvar in a b; do echo "body"; done; }
l; echo "for: status $? ${loopvar-unset}"
p() { ret "result"; }
v := p; echo "ret is the shell's own: $v"
dl() { local later; later=1; inner; echo "declared local, set later: $later"; }
inner() { later=2; }
dl; echo "and not a global: ${later-unset}"
o() { newagain=1; }
strict off vars
o; echo "after strict off vars: ${newagain-unset}"
X
"$HIBR" vars.hibr 2>&1

cat > lax.hibr <<'X'
laxf() { made_by_lax=1; }
X
cat > strictlib.hibr <<'X'
strict vars
libf() { made_by_lib=1; }
X
cat > mixed.hibr <<'X'
strict vars
. ./lax.hibr
. ./strictlib.hibr
laxf; echo "a lax file's function, called from a strict one: ${made_by_lax-unset}"
libf; echo "a strict file's function: ${made_by_lib-unset}"
X
"$HIBR" mixed.hibr 2>&1

cat > loader.hibr <<'X'
strict vars
load() { . ./toplevel.hibr; }
load; echo "a sourced file's top level is not in a function: $tl"
X
echo 'tl=set' > toplevel.hibr
"$HIBR" loader.hibr 2>&1

cat > exp.hibr <<'X'
strict expansion
x="a b"
set -- $x; echo "expansion: $# word"
strict -p
X
"$HIBR" exp.hibr 2>&1
"$HIBR" -c 'strict nonsense' 2>&1; echo "a bad check: $?"

cat > lines.hibr <<'X'
echo "line $LINENO"
lf() {
	echo "in a function, its own line $LINENO"
}
lf
echo "$(echo "in a substitution $LINENO")"
X
"$HIBR" lines.hibr 2>&1

cd /
rm -rf "$d"
