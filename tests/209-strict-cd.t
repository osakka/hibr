# `cd` in a strict function is the shell doing its own bookkeeping, not
# the script creating a global (Gitea #251).
#
# `cd` sets PWD and OLDPWD, and `strict vars` refused both as globals a
# function was creating -- which does not merely log: the refusal *fails*
# the cd, so `out=$(cd "$d" && thing)` short-circuits and `out` comes back
# empty. Sheet runs every formula that way, so on any desktop whose
# session had no OLDPWD in its environment -- a fresh PAM one from the
# login screen has none -- every formula in the spreadsheet evaluated to
# nothing, always, with the reason only in the desktop's log.
#
# v_shellvar is the list of names the shell manages for the script; RET,
# M, BASH_REMATCH, PIPESTATUS and REPLY were all on it and these two were
# not. The environment is stripped here because the bug needs the name to
# be *created*: with PWD and OLDPWD already set, nothing is.

d=${TMPDIR:-/tmp}/hibr-sc-$$
mkdir -p "$d"
cat > "$d/s.hibr" <<'S'
strict
fn t_cd() {
	local o
	o=$(cd /tmp && echo ran)
	echo "cd: [$o]"
	return 0
}
fn t_pushd() {
	local o
	o=$(pushd /tmp > /dev/null && echo ran)
	echo "pushd: [$o]"
	return 0
}
t_cd
t_pushd
S
( cd "$d" && env -u PWD -u OLDPWD "$HIBR" s.hibr 2>&1 )
echo "status $?"

# And a name the script really does create is still refused, or the
# exemption would have turned strict vars off rather than corrected it.
cat > "$d/g.hibr" <<'S'
strict
fn t_make() {
	mine=1
	echo "made: $mine"
	return 0
}
t_make
S
# Run from inside the folder, so the diagnostic names the script
# rather than a scratch path with this process's pid in it: a
# recorded test has to be the same bytes every run.
( cd "$d" && env -u PWD -u OLDPWD "$HIBR" g.hibr 2>&1 )
echo "status $?"

rm -rf "$d"
