# varwatch: the shell's variable table tells a module when a watched global
# changes (ADR 0050, the one write path), so what moved is known without any
# writer saying so. Every way a script can write a variable is driven here on
# a watched global, and each section prints the variable before and after
# and what `varwatch moved` reported for that one form alone -- so a write
# path that misses the hook shows as `moved []` on its own labelled line.
# Then the cases where nothing must be reported: a local shadowing a watched
# global, a dropped watch, and an unwatched variable written every way.
# Recorded: bash has no varwatch.
#
# A report is the entries of `varwatch moved`, sorted, each `NAME:KEY` with
# the tab shown as a colon, and `NAME:` for the whole variable.

mod load ./build/mods/varwatch.so
mod load ./build/mods/csv.so
d=$(mktemp -d)

# What moved since the last ask, as one sorted line, into `got`.
moved() {
	local m e lines=
	m := varwatch moved
	for e in "${m[@]}"; do
		[ -n "$e" ] || continue
		case $e in
		*$'\t'*) lines+="${e%%$'\t'*}:${e#*$'\t'}"$'\n' ;;
		*) lines+="notab($e)"$'\n' ;;
		esac
	done
	got=$(printf '%s' "$lines" | LC_ALL=C sort -u | paste -sd' ' -)
}

# One variable as declare -p has it, or that it is not there.
show() {
	declare -p "$1" 2> /dev/null || echo "$1 is unset"
}

# A section starts: forget what the setup moved, and show the variables.
begin() {
	label=$1
	shift
	names=("$@")
	echo "== $label"
	moved
	local n
	for n in "${names[@]}"; do
		show "$n" | sed 's/^/  before /'
	done
}

# A section ends: what moved, and the variables after. With -n only the
# names that moved are shown, for a write whose report the spec leaves free
# to be the whole variable or each key it touched -- either is right, and a
# write that reached no hook is still an empty report.
end() {
	moved
	local n
	for n in "${names[@]}"; do
		show "$n" | sed 's/^/  after  /'
	done
	if [ "${1-}" = -n ]; then
		got=$(printf '%s\n' $got | sed 's/:.*//' | LC_ALL=C sort -u | paste -sd' ' -)
		echo "  moved  [$got] (names)"
	else
		echo "  moved  [$got]"
	fi
}

# The names watched, sorted, whatever separates them.
listed() {
	echo "watched: $(varwatch list | tr -s ' \t' '\n\n' | sed '/^$/d' | LC_ALL=C sort | paste -sd' ' -)"
}

S=one
declare -ga A=(a b)
declare -gA H
H["k"]="old"
G=gone
varwatch add S A H G
listed

s0 := varwatch seq
S=seq
s1 := varwatch seq
[ "$s0" != "$s1" ] && echo "seq moved on a watched write: yes" || echo "seq moved on a watched write: no"
moved

echo "== the printed form, unbound"
S=printed
H["k"]="printed"
varwatch moved > "$d/raw"
LC_ALL=C sort "$d/raw" | sed -n l
varwatch moved > "$d/raw"
echo "  asked again: $(( $(wc -l < "$d/raw") )) lines"

S=one
begin "plain assignment" S
S=two
end

begin "+= on a scalar" S
S+=more
end

begin "+= on an array" A
A+=(c)
end -n

H=()
H["k"]="old"
begin "a[k]=" H
H["k"]="new"
end

begin "a[k][j]= nested" H
H["k"]["j"]="deep"
end

H=()
H["k"]="old"
H["o"]="other"
begin "unset 'a[k]'" H
unset 'H[k]'
end

H["k"]="old"
begin "unset a[\"k\"]" H
unset H["k"]
end

begin "a=(...)" A
A=(x y z)
end -n

begin "declare -A a=(...)" H
declare -A H=(["p"]=1 ["q"]=2)
end -n

begin "read -a" A
read -a A <<< "r1 r2"
end -n

begin "read -r" S
read -r S <<< "a line"
end

printf '%s\n' m1 m2 > "$d/lines"
begin "mapfile -t" A
mapfile -t A < "$d/lines"
end -n

begin "json parse a" H
json parse H '{"j":1,"t":"x"}'
end -n

H["k"]="old"
begin "json parse a[k] TEXT" H
json parse H["k"] '{"x":[1,2]}'
end

H["k"]="old"
begin "x := cmd binding a map into a[k]" H
H["k"] := csv split "p,q"
end

H["k"]="old"
viaref() {
	local -n r=H
	r["k"]="via"
}
begin "nameref write (local -n r=a; r[k]=v)" H
viaref
end

begin "printf -v" S
printf -v S '%s-%s' p q
end

H["n"]=1
begin "(( a[k]++ ))" H
(( H["n"]++ ))
end

S=""
begin "\${a:=d}" S
: "${S:=dflt}"
end

fromfn() {
	declare -g S=fromfn
}
begin "declare -g a=... in a function" S
fromfn
end

handback() {
	local l
	l=$(H=(); H["from"]="job"; declare -p H)
	eval "${l/#declare /declare -g }"
}
begin "a job's hand-back (eval \"declare -g ...\")" H
handback
end -n

S=keep
begin "temporary prefix a=x on a builtin" S
S=tmp true
end

declare +x S
nothing() { :; }
begin "temporary prefix a=x on a function" S
S=tmp nothing
end

# hibr leaves a variable exported after a temporary prefix on a builtin or
# a function, where bash does not; that is not this test's business, so the
# attribute is taken off again before it reaches every later line.
declare +x S
begin "still watched after a prefix" S
S=after
end

begin "unset a" G
unset G
end

begin "unset then declared again: the watch survives" G
G=back
end

begin "and keeps surviving" G
G=again
end
listed

unset G
begin "unset then declare -gA again" G
declare -gA G
G["k"]="v"
end -n

H=()
begin "33 keys of one variable: more than 32, so the whole variable" H
i=0
while [ $i -lt 33 ]; do
	H["k$i"]=1
	i=$((i + 1))
done
end

S=global
H=()
H["k"]="global"
shadow() {
	local S
	S=inner
	S+=x
	printf -v S '%s' p
	read -r S <<< "read"
	local -A H
	H["k"]="inner"
	H["k"]["j"]="deeper"
	unset H["k"]
	H["k"]="inner"
	inner
	echo "  inside: S=$S H[k]=${H["k"]} H[d]=${H["d"]}"
}
inner() {
	S=dynamic
	H["d"]="dynamic"
}
begin "a local shadowing a watched global: writes to the local are not reported" S H
shadow
end

begin "the global written once the local has gone" S
S=global2
end

unset G
pendlocal() {
	local G=x
	G=y
	G+=z
	echo "  inside: G=$G"
}
begin "a local made while a watched global is pending re-creation" G
pendlocal
end

begin "and the global made after it is still watched" G
G=back
end

varwatch drop S
begin "a dropped watch" S
S=dropped
end
listed
varwatch add S

begin "watched again once added back" S
S=readded
end

C=one
declare -ga CA=(a b)
declare -gA CX=(["k"]=1)
declare -gA CH
CH["k"]="old"
viaref2() {
	local -n r=CH
	r["k"]="via"
}
fromfn2() {
	declare -g C=fromfn
}
handback2() {
	local l
	l=$(CH=(); CH["from"]="job"; declare -p CH)
	eval "${l/#declare /declare -g }"
}
CH["n"]=1
# A map grafted on after the assignment saw "" go to "": json parse of an
# object, NAME=(), and := of a map, each into a watched empty scalar.
E1=
E2=
E3=
varwatch add E1 E2 E3
begin "json parse of an object into a watched empty scalar" E1
json parse E1 <<< '{"a":"1"}'
end -n

begin "NAME=() on a watched empty scalar" E2
E2=()
end -n

begin ":= of a map into a watched empty scalar" E3
E3 := csv split "p,q"
end -n

c0 := varwatch seq
begin "an unwatched variable, written every way" C CA CH CX
declare -A CX=(["p"]=1 ["q"]=2)
C=two
C+=more
CA+=(c)
CH["k"]="new"
CH["k"]["j"]="deep"
unset 'CH[k]'
CH["k"]="old"
unset CH["k"]
CA=(x y z)
read -a CA <<< "r1 r2"
read -r C <<< "a line"
mapfile -t CA < "$d/lines"
json parse CH '{"j":1}'
json parse CH["k"] '{"x":[1,2]}'
CH["m"] := csv split "p,q"
viaref2
printf -v C '%s-%s' p q
CH["n"]=1
(( CH["n"]++ ))
C=""
: "${C:=dflt}"
fromfn2
handback2
C=tmp true
C=tmp nothing
unset C
C=back
end
c1 := varwatch seq
[ "$c0" != "$c1" ] && echo "seq moved on unwatched writes: yes" || echo "seq moved on unwatched writes: no"

begin "a watched write after all of that" S
S=last
end

rm -rf "$d"
