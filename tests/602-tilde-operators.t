c() { echo "$# [$*]"; }
y="p q"
on=1

c ${x:-"a b"} ${x:-a b} ${x:-$y} ${x:-"$y"} ${x:-"*"} ${on:+pre$y}
u=; c ${u:=a b}; echo "u=[$u]"
c ${x:-$y$y} ${on:+"$y"x}

x=~; y2=a:~/b:~; w=~/"q r"; q="~"
f() { local l=~/l; echo "$l"; }
f
export E=~/bin:~/x
v=u; t=~$v
case ~ in /*) k=abs ;; *) k=lit ;; esac
[[ ~ == /* ]] && d=abs || d=lit
[ "$x" = "$HOME" ] && echo "x is home"
[ "$y2" = "a:$HOME/b:$HOME" ] && echo "after colons too"
[ "$w" = "$HOME/q r" ] && echo "before a quoted part"
[ "$E" = "$HOME/bin:$HOME/x" ] && echo "through export"
echo "$q $t $k $d"
[ ${u2:-~} = "$HOME" ] && echo "in a default"
echo "quoted, it stays: ${u2:-~}"
echo $((~0)) $((~5 + 1))

g() {
	local n=$1
	[ "$n" -le 1 ] && { echo 1; return; }
	local sub=$(g $((n - 1)))
	echo $((n * sub))
}
echo "g 6 = $(g 6)"
