name=world
cat <<EOF
hello $name
sum is $((3+4))
literal \$name
EOF
cat <<'RAW'
no $expansion here
RAW
run() {
	cat <<-IN
		indented $1
		second line
	IN
}
run tabbed
wc -l <<END
a
b
c
END
