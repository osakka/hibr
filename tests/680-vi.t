# The editor's argument handling and its need for a display, which is all
# run.sh can reach; the editing itself is tests/vi.py, through a pty.
mod load ./build/mods/vi.so && echo "vi loads on its own"
vi /etc/hostname 2>/dev/null; echo "vi without a display rc=$?"
mod load ./build/mods/console.so && echo "console offers one"
vi a b 2>/dev/null; echo "too many files rc=$?"
mod drop vi > /dev/null && mod drop console > /dev/null && echo "dropped"
