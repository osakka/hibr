x=global
show() { echo "in show: x=$x"; }
outer() {
  local x=outer
  echo "outer sees $x"
  show
  inner
  echo "outer still $x"
}
inner() {
  local x=inner
  echo "inner sees $x"
  show
}
outer
echo "after: x=$x"

fact() {
  local n=$1
  local acc=1
  if [ "$n" -le 1 ]; then echo 1; return; fi
  local sub=$(fact $((n-1)))
  echo $((n * sub))
}
echo "fact 7 = $(fact 7)"
echo "n after fact = [$n] acc=[$acc] sub=[$sub]"

count() {
  local i=0
  local total=0
  while [ $i -lt 5 ]; do
    total=$((total + i))
    i=$((i + 1))
  done
  echo "total=$total"
}
i=99
count
echo "i preserved = $i"

nounset() {
  local fresh
  echo "fresh is [${fresh-unset}]"
  fresh=assigned
  echo "fresh now [$fresh]"
}
fresh=outside
nounset
echo "fresh restored = $fresh"

shadow() { local v=1; deeper; echo "shadow v=$v"; }
deeper() { echo "deeper sees v=$v"; v=changed; }
v=top
shadow
echo "top v=$v"

export KEEP=exported
touch_export() { local KEEP=temp; echo "inside [$KEEP]"; }
touch_export
echo "after [$KEEP]"
env | grep '^KEEP=' || echo "KEEP LOST FROM ENVIRONMENT"
