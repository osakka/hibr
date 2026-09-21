mod load ./mods/prompt.so
root=$(pwd)

fn show() {
	p := prompt render
	str replace "$p" $'\e' "<E>" o
	echo "[$o]"
}

echo "== literals and escapes"
PROMPT[format]='plain text'
show
PROMPT[format]='esc \$ \[ \] \( \) \\ end'
show
PROMPT[format]='one\ntwo\tthree'
show

echo "== unknown variables vanish"
PROMPT[format]='a$nosuchsegment b'
show

echo "== styles"
PROMPT[format]='[red](red)|[bold](bold green)|[hex](#ff8800)|[n256](fg:117)|[plain](none)'
show
PROMPT[format]='[outer [inner](bold red) back](blue)'
show

echo "== conditional groups"
PROMPT[status][format]='<$symbol$int>'
PROMPT[status][symbol]='!'
PROMPT[status][style]=none
PROMPT[format]='before($status)after'
true
show
false
show

echo "== segment gating and overrides"
PROMPT[format]='<$char>'
PROMPT[char][style]=none
PROMPT[char][symbol]='%'
true
show
PROMPT[char][error_style]='bold red'
false
show
PROMPT[char][disabled]=1
show
PROMPT[char][disabled]=0

echo "== duration"
PROMPT[format]='<$duration>'
PROMPT[duration][style]=none
PROMPT[duration][symbol]='t='
DURATION=0
show
DURATION=1500
show
PROMPT[duration][min]=100
DURATION=1500
show
DURATION=45000
show
DURATION=3725000
show
DURATION=900
show
DURATION=0
PROMPT[duration][min]=2000

echo "== environment segments"
PROMPT[format]='<$venv><$nix>'
PROMPT[venv][style]=none
PROMPT[nix][style]=none
show
VIRTUAL_ENV=/tmp/envs/myproject
show
IN_NIX_SHELL=pure
show
unset VIRTUAL_ENV
unset IN_NIX_SHELL

echo "== git"
d=/tmp/hibr-prompt-$$
rm -rf "$d"
mkdir -p "$d/repo/.git/refs/heads" "$d/repo/sub/deep" "$d/repo/.git/logs/refs"
printf 'ref: refs/heads/feature/x\n' > "$d/repo/.git/HEAD"
cd "$d/repo/sub"
PROMPT[format]='<$git>'
PROMPT[git][style]=none
PROMPT[git][state_style]=none
show

echo "-- symbol"
PROMPT[git][symbol]='on '
show
PROMPT[git][symbol]=''

echo "-- merge"
printf '0000000000000000000000000000000000000000\n' > "$d/repo/.git/MERGE_HEAD"
show
rm -f "$d/repo/.git/MERGE_HEAD"

echo "-- rebase with progress"
mkdir -p "$d/repo/.git/rebase-merge"
printf '2\n' > "$d/repo/.git/rebase-merge/msgnum"
printf '5\n' > "$d/repo/.git/rebase-merge/end"
show
rm -rf "$d/repo/.git/rebase-merge"

echo "-- cherry-pick, revert, bisect"
printf 'x\n' > "$d/repo/.git/CHERRY_PICK_HEAD"; show
rm -f "$d/repo/.git/CHERRY_PICK_HEAD"
printf 'x\n' > "$d/repo/.git/REVERT_HEAD"; show
rm -f "$d/repo/.git/REVERT_HEAD"
printf 'x\n' > "$d/repo/.git/BISECT_LOG"; show
rm -f "$d/repo/.git/BISECT_LOG"

echo "-- detached head"
printf '3f786850e387550fdab836ed7e6dc881de23001b\n' > "$d/repo/.git/HEAD"
show
printf 'ref: refs/heads/main\n' > "$d/repo/.git/HEAD"

echo "-- stash count"
PROMPT[git][format]='[$branch]($style)( stash=$stash)'
show
printf 'a\nb\nc\n' > "$d/repo/.git/logs/refs/stash"
show
rm -f "$d/repo/.git/logs/refs/stash"

echo "-- repo relative directory"
PROMPT[format]='<$dir>'
PROMPT[dir][style]=none
show
cd "$d/repo/sub/deep"
show
PROMPT[dir][truncate]=1
show
PROMPT[dir][truncate]=3
oldhome=$HOME
HOME="$d/repo"
cd "$d/repo/sub"
PROMPT[dir][repo_root]=0
show
PROMPT[dir][repo_root]=1
HOME=$oldhome

echo "-- outside a repository"
cd "$d"
PROMPT[format]='<$git>after'
show

echo "== newline option"
cd "$root"
PROMPT[format]='tail'
PROMPT[newline]=1
show
PROMPT[newline]=0

echo "== segment list is sorted by use"
prompt list | head -4

rm -rf "$d"
