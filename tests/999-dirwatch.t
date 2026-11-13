# The dirwatch module (Gitea #545's sibling, the owner's): folders watched by
# the kernel, so a change wakes whoever watches its descriptor -- Files
# reads a folder again the moment something changes in it, with no polling.
case $(uname) in Linux) ;; *) echo "skip: inotify is Linux's"; exit 0 ;; esac
mod load ./build/mods/dirwatch.so || exit 1
d=$(mktemp -d)
w := dirwatch add "$d"
echo "a watch is a number: $([[ $w =~ ^[0-9]+$ ]] && echo yes)"
dirwatch read
echo "nothing has happened: status $?"
: > "$d/new.txt"
r := dirwatch read
echo "a file made is seen: ${RET[*]} == $w"
dirwatch read
echo "and read once: status $?"
mv "$d/new.txt" "$d/renamed.txt"; rm "$d/renamed.txt"
r := dirwatch read
echo "a rename and a removal are seen, as one: ${#RET[@]}"
again := dirwatch add "$d"
echo "the same folder twice is the same watch: $([ "$again" = "$w" ] && echo yes)"
dirwatch drop "$w"
: > "$d/after"
dirwatch read
echo "a dropped watch says nothing, its own end included: status $?"
dirwatch drop "$w"
echo "dropping it again fails: status $?"
f := dirwatch fd
echo "the descriptor is not a standard one: $([ "$f" -gt 2 ] && echo yes)"
dirwatch add "$d/no-such-folder" 2> /dev/null
echo "a folder that is not there is refused: status $?"
rm -rf "$d"
