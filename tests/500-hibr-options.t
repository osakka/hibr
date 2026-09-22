echo "-- extglob needs no shopt, unlike bash --"
case abc in @(abc|xyz)) echo "at group: match" ;; *) echo "at group: no" ;; esac
case abc in @(x|y)) echo "at miss: match" ;; *) echo "at miss: no" ;; esac
case foo.txt in !(*.log)) echo "neg group: match" ;; *) echo "neg group: no" ;; esac
[[ aaa == *(a) ]] && echo "star: yes"
[[ aaa == +(a) ]] && echo "plus: yes"
[[ "" == +(a) ]] || echo "plus needs one: yes"
[[ ab == ?(a)b ]] && echo "opt: yes"
[[ b == ?(a)b ]] && echo "opt empty: yes"
[[ abc == !(xyz) ]] && echo "neg: yes"
[[ xyz == !(xyz) ]] || echo "neg excludes: yes"
[[ afoob == a@(foo|bar)b ]] && echo "embedded: yes"
[[ abazb == a@(foo|bar)b ]] || echo "embedded miss: yes"

echo "-- one option namespace, reached either way --"
shopt -s nullglob
shopt nullglob
set +o nullglob
shopt nullglob
set -o errexit
shopt errexit
set +e
shopt errexit
shopt -s noclobber
set +o noclobber
shopt noclobber

echo "-- options that do not move say so --"
shopt -u extglob
echo "unset extglob: $?"
shopt -s pipefail
echo "set pipefail: $?"
shopt extglob globstar expand_aliases pipefail

echo "-- nullglob and failglob --"
shopt -s nullglob
set -- /nonexistentdir/*
echo "nullglob count: $#"
shopt -u nullglob
set -- /nonexistentdir/*
echo "without it: $#"

echo "-- declared types validate, where -i coerces --"
declare -i bashy
bashy=notanumber
echo "bash style -i: $bashy"
declare int strict=5
echo "typed ok: $strict"
declare num f=1.5
echo "num ok: $f"
declare -p strict
declare int bad=1
bad=oops 2>/dev/null
echo "a declared type refuses: rc=$? value still $bad"
readonly frozen=1
frozen=2 2>/dev/null
echo "readonly refuses: rc=$? value still $frozen"
echo "and the script carries on"
