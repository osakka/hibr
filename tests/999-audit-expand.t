# The smaller expansion divergences of the core audit (Gitea #442),
# compared against bash: set -u under every operator, ${a[k]:=v} into its
# key, a tilde prefix that runs into a quote, an unclosed [, brace ranges,
# globstar order, subscripts, sparse slices, and & in a replacement.
self=$(readlink /proc/$$/exe)
"$self" -c 'set -u; echo "a${zz#a}b"; echo after' 2> /dev/null; echo "u1 $?"
"$self" -c 'set -u; a=(1); echo "${a[3]}"' 2> /dev/null; echo "u2 $?"
set -u; x=1; echo ${x[0]} ${RANDOM[0]:+r} "${zz:-d}" "${zz+no}"; set +u
unset b; echo "${b[1]:=zz}" "${!b[@]}" "${b[1]}"
"$self" -c 'echo "${1:=x}"' 2> /dev/null; echo "u3 $?"
echo ~"x" ~$USER
case "[a" in [a) echo 1y;; *) echo 1n;; esac
echo {1..010..4} {1..3x} {1..} {1..a} {-02..2} {a..e..2} {0..-2}
t=${TMPDIR:-/tmp}/hibr-glob-$$; mkdir -p "$t/a/b/c"; touch "$t/top.c" "$t/a/one.c" "$t/a/b/two.c" "$t/a/b/c/three.c"
shopt -s globstar; (cd "$t" && echo **/*.c); rm -rf "$t"
a=(x y z); k=1; echo ${a[k?2:0]} ${a[ 1 ]} ${a[~-2]}
s=([0]=a [5]=b [6]=c); echo "${s[@]:2:1}" "${s[@]:5}" "${s[@]: -1}" "${s[@]:7}"
v=abc; echo "${v/b/[&]}" "${v/b/[\&]}" "${v/b/"[&]"}" ${v//?/<&>} ${v/#a/[&]}
h="a<b&c>"; h=${h//&/"&amp;"}; h=${h//</"&lt;"}; echo "$h"
e=(pa pb); echo ${e[@]/p/<&>}
shopt -u patsub_replacement; echo ${v/b/[&]}; shopt -s patsub_replacement
