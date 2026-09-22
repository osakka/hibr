# One module offering a table of functions to another. Modules are opened
# RTLD_LOCAL on purpose, so this registry is the only way across -- and since
# a tool asks for an interface rather than a module, the shell will go and
# find a provider that is not loaded yet.
export HIBR_MODPATH=./build/mods

mod load most > /dev/null && echo "most loaded on its own"
echo "nothing offers a display yet: $(mod list | grep -c '^console ')"

# asking for one brings in whatever says it offers it
most /etc/hostname 2>/dev/null; echo "most without a terminal rc=$?"
echo "a display was found and loaded: $(mod list | grep -c '^console ')"

# the offer is visible
mod avail | grep -c 'offers display'

# dropping the provider withdraws the offer, and it is found again next time
mod drop console > /dev/null && echo "provider dropped"
most /etc/hostname 2>/dev/null; echo "most again rc=$?"
echo "found again: $(mod list | grep -c '^console ')"

# an interface nothing offers is refused rather than hunted for ever
mod drop all > /dev/null
echo "all dropped: $(mod list | grep -c .)"
