# Two modules on the path declare one builtin, as hibr's own web (Chromium)
# and hibr-browser's webkit both declare `web`, and only one of them offers
# the interface of that name. Which one a bare `web` loaded was a bsearch
# landing anywhere in a run of equal names, so it depended on what else was
# installed; and `need web` was met by any loaded module *named* web, so once
# Chromium's was in, WebKit was never looked for and the Browser said to
# install the package that was installed. The one offering the interface is
# chosen now, and `need` loads it beside a same-named module that offers
# nothing. Here the plain one sorts first on the path, which is the order
# that picked it before.
d=/tmp/hibr-modtwin-$$
mkdir -p "$d/two" "$d/one"
if { tools/tccld -shared -fPIC -Iinclude -o "$d/two/a.so" tests/modtwin.c '-DTW_WHO="plain"' &&
	tools/tccld -shared -fPIC -Iinclude -DTW_PROV -o "$d/two/b.so" tests/modtwin.c '-DTW_WHO="provider"'; } 2> /dev/null ||
   { gcc -shared -fPIC -Iinclude -o "$d/two/a.so" tests/modtwin.c '-DTW_WHO="plain"' &&
	gcc -shared -fPIC -Iinclude -DTW_PROV -o "$d/two/b.so" tests/modtwin.c '-DTW_WHO="provider"'; } 2> /dev/null
then
	cp "$d/two/a.so" "$d/one/a.so"
	echo "a bare command loads: $(HIBR_MODPATH="$d/two" "$HIBR" -c 'twin')"
	echo "need, with the plain one loaded first: $(HIBR_MODPATH="$d/two" "$HIBR" -c "mod load $d/two/a.so; need twin; echo rc=\$?; twin")"
	echo "need, with no provider at all: $(HIBR_MODPATH="$d/one" "$HIBR" -c "mod load $d/one/a.so; need twin; echo rc=\$?; twin")"
else
	echo "no compiler, skipped"
fi
rm -rf "$d"
