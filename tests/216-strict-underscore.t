# `read ... _` in a strict function discards a field; it is not the script
# creating a global (Gitea #281).
#
# _ is the shell's own parameter, and is normally inherited from the
# environment, since a parent shell exports it. A session the login screen
# starts has a cleared environment with no _ in it, so `strict vars`
# refused the read -- which fails it, status 1, not merely logs. Task
# Manager read each process's uid from /proc/<pid>/status with
# `while read -r k v _`, so the loop stopped before a line and it handed
# back an empty uid, which `read -r uid rss <<< " 0"` turned into 0: every
# process in that session labelled root. The environment is stripped here
# because the bug needs _ to be *created*. #251 was PWD and OLDPWD, the
# same shape.

d=${TMPDIR:-/tmp}/hibr-su-$$
mkdir -p "$d"
cat > "$d/s.hibr" <<'S'
strict
fn t_read() {
	local k v n=0
	while read -r k v _; do
		n=$((n + 1))
	done <<< $'Uid: 1000 1000\nGid: 1000 1000'
	echo "lines: $n"
	return 0
}
t_read
S
( cd "$d" && env -u _ "$HIBR" s.hibr 2>&1 )
echo "status $?"

# A name the script really does create is still refused.
cat > "$d/g.hibr" <<'S'
strict
fn t_make() {
	read -r a mine <<< "x y"
	echo "made: $mine"
	return 0
}
t_make
S
( cd "$d" && env -u _ "$HIBR" g.hibr 2>&1 )
echo "status $?"

rm -rf "$d"
