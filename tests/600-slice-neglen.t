# A negative length counts back from the end. bash refuses this for lists
# and allows it for scalars; hibr allows it for both, the same way.
a=(p q r s)
set -- w x y z
v=abcdef

echo "scalar   [${v:1:-1}]"
echo "array    [${a[*]:1:-1}]"
echo "params   [${*:1:-1}]"
echo "all-but  [${a[*]:0:-1}]"
echo "empty    [${a[*]:1:-3}]"
echo "past     [${a[*]:0:-9}]"
