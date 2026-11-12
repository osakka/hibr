# The shell finds its own modules beside where it runs -- bin/hibr beside
# lib/hibr -- as well as in the folder it was built for, so a Homebrew keg or
# a moved prefix still has them (Gitea #503: on a Mac every module was "not
# found on the module path"). And a module file that is there and will not
# load says so, rather than that it is missing.
d=/tmp/hibr-modexe-$$
mkdir -p "$d/bin" "$d/lib/hibr" "$d/elsewhere"
cp "$HIBR" "$d/bin/hibr"
cp build/mods/sys.so "$d/lib/hibr/sysbeside.so"
printf 'not a module\n' > "$d/lib/hibr/broken.so"
cd "$d/elsewhere"
echo "beside the shell: $(HIBR_MODPATH= "$d/bin/hibr" -c 'mod load sysbeside && echo loaded')"
r=$(HIBR_MODPATH= "$d/bin/hibr" -c 'mod load broken' 2>&1)
case $r in
*"found and not loaded: "*"$d/lib/hibr/broken.so"*) echo "a file that will not load: says why" ;;
*) echo "a file that will not load: $r" ;;
esac
# And `need`, which is what the desktop's `need display` said on the Mac:
# nothing offers it, and the module that might have would not load.
r=$(HIBR_MODPATH= "$d/bin/hibr" -c 'need nosuchiface' 2>&1)
case $r in
*"would not load: "*"$d/lib/hibr/broken.so"*) echo "need names the module that would not load" ;;
*) echo "need: $r" ;;
esac
r=$(HIBR_MODPATH= "$d/bin/hibr" -c 'mod load nosuchmodule' 2>&1)
echo "${r#hibr: }"
cd /
rm -rf "$d"
