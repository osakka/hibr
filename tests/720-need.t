# A script says what it cannot run without, the same way a module says what it
# offers. The shell resolves both through one registry.
export HIBR_MODPATH=./build/mods

echo "start with nothing: $(mod list | grep -c .)"

# an interface: found on the module path and loaded
need display; echo "need display rc=$?"
echo "now loaded: $(mod list | grep -c .)"
mod list | grep -c '^console '

# asking again is not an error, and loads nothing twice
need display; echo "again rc=$? loaded=$(mod list | grep -c .)"

# several at once, and a module by its own name
need display highlight; echo "two interfaces rc=$? loaded=$(mod list | grep -c .)"
need sys; echo "a module by name rc=$? loaded=$(mod list | grep -c .)"

# something nothing offers is refused, and says which
need teleporter 2>/dev/null; echo "unmet rc=$?"
need display teleporter 2>/dev/null; echo "one unmet in a list rc=$?"
need 2>/dev/null; echo "no arguments rc=$?"

# app is a declaration and nothing more
app calc "A calculator"; echo "app rc=$? name=[$APP_NAME] desc=[$APP_DESC]"
app 2>/dev/null; echo "app with no name rc=$?"

mod drop all > /dev/null && echo "dropped"
