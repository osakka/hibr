# A strict function that loads a module: the module's own init may set a
# variable (sys sets SYS_MOD), which is the module's, not a global the
# script created. Strict vars refused it, and the refusal failed the
# command that loaded the module -- `need` answered 1 for a module it had
# just loaded, so the catalogue called sys missing (Gitea #490).
export HIBR_MODPATH=./build/mods
strict

fn check() {
	need sys
	echo "need sys from a strict function: $?"
	sleepms 1
	echo "and its builtin runs: $?"
}
check

fn stillstrict() {
	CREATED_HERE=1
	echo "a global the script creates is still refused: $?"
}
stillstrict 2> /dev/null
