classify() {
  case $1 in
    (paren) echo "leading paren clause" ;;
    a|b|c)  echo "one of abc" ;;
    *.tar.gz) echo "tarball" ;;
    /*)     echo "absolute path" ;;
    ?)      echo "single char" ;;
    *)      echo "no idea" ;;
  esac
}
for a in paren b archive.tar.gz /usr/bin z hello; do classify "$a"; done

pat="he*"
subject=hello
case $subject in
  $pat) echo "variable pattern matched" ;;
  *) echo "variable pattern missed" ;;
esac

case abc in
  a*) echo "first" ;&
  zzz) echo "fell through unconditionally" ;;
  *) echo "not reached" ;;
esac

case abc in
  a*) echo "one" ;;&
  *c) echo "two: kept testing" ;;&
  nomatch) echo "three: not reached" ;;
  *) echo "four: default after continue" ;;
esac

i=0
while [ $i -lt 3 ]; do
  case $i in
    0) echo "zero" ;;
    1) echo "one";;
    *) echo "many" ;;
  esac
  i=$((i+1))
done

case outer in
  o*) case inner in
        i*) echo "nested matched" ;;
        *) echo "nested default" ;;
      esac ;;
esac

case x in
  y) echo no ;;
esac
echo "empty result status=$?"

d=/tmp/hibr-case-$$
mkdir -p $d
case redirected in
  r*) echo "into a file" ;;
esac > $d/out
cat $d/out
rm -rf $d
