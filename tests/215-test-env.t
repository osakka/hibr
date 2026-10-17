# The harness gives every test a shell environment of its own (Gitea #253).
# One gate failed 090-module, 199-sysmods and 450-drop together, each
# finding a module that should have been gone, and passed the next time:
# the harness passed on whatever the shell that started it held, and
# HIBR_MODPATH=build/mods there fails the first and the last exactly as
# that gate did. Recorded, because the answer is the harness's own.
#
# HIBR_SYSDIR is cleared too, and not checked here: the shell sets it at
# startup from its compiled value whenever the environment did not, so it
# is always set inside a test and only its value could tell the two apart.
for v in HIBR_MODPATH HIBR_MODULES HIBR_HOLD; do
	if [ -n "${!v+x}" ]; then
		echo "$v is set, and a test did not set it"
	else
		echo "$v: not inherited"
	fi
done
