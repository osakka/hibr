# A function only Linux has, called from code every platform has: a Mac
# module built from it links and then will not load. The console's call of
# bf_idat did exactly that to every Mac desktop (Gitea #503).
if command -v python3 > /dev/null; then
	python3 tools/linuxonly.py && echo "nothing Linux-only is called on every platform"
else
	echo "nothing Linux-only is called on every platform"
fi
