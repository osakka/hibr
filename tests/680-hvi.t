# The editor's argument handling, which is all run.sh can reach; the editing
# itself is tests/hvi.py, through a pty.
export HIBR_MODPATH=./build/mods

mod load hvi > /dev/null && echo "hvi loads on its own"
hvi /etc/hostname 2>/dev/null; echo "hvi without a terminal rc=$?"
hvi a b 2>/dev/null; echo "too many files rc=$?"
mod drop all > /dev/null && echo "dropped"
