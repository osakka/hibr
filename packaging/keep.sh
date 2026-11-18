# packaging/keep.sh -- what every package repository keeps, sourced by
# packaging/{deb,rpm,arch}/publish.sh so the three cannot drift apart.
#
# A repository holds the newest KEEP (3) versions of each package for each
# architecture and nothing older, and its history is one commit: every
# publish replaces the history with a single commit of what the repository
# holds now and force-pushes it (owner, 2026-11-18). Git history had become
# every package ever published -- 5.8 GB of it under hibr-apt's 195 MB of
# files -- and pruning files alone never shrinks that. Nothing older than
# the newest three can be fetched from any of them after a publish.

KEEP=${PKG_KEEP:-${APT_KEEP:-3}}

# Remove all but the newest KEEP of each package for each architecture.
# Reads "name arch version path" a line on stdin; a path's .sig, if there
# is one, goes with it.
keep_prune() {
	sort -k1,1 -k2,2 -k3,3Vr |
		awk -v keep="$KEEP" '{ k = $1 " " $2; if (++n[k] > keep) print $4 }' |
		while read -r f; do
			rm -f "$f" "$f.sig"
			echo "== pruned ${f##*/}"
		done
}

# Commit what the repository in the current folder holds as its only
# commit, push it over what the remote had, and let the local copy forget
# the rest so this disk shrinks too. Status 1, the commit kept, when the
# push fails.
keep_publish() {
	keep_msg=$1
	keep_branch=$(git branch --show-current)
	git add -A
	keep_tree=$(git write-tree)
	keep_commit=$(git commit-tree "$keep_tree" -m "$keep_msg")
	git reset -q --soft "$keep_commit"
	if ! timeout 300 git push -q --force origin "HEAD:$keep_branch" 2> push.err; then
		cat push.err >&2
		rm -f push.err
		echo "publish: committed in $(pwd), not pushed" >&2
		return 1
	fi
	rm -f push.err
	git reflog expire --expire=now --all
	git gc -q --prune=now
	echo "== history is one commit; $(du -sh .git | cut -f1) of it here"
	return 0
}
