# A tool asks for an interface, not for a module. When nothing offers it yet,
# the shell looks along the module path for something that says it would.
export HIBR_MODPATH=./build/mods

mod load most > /dev/null && echo "most loaded on its own"
mod list | grep -c '^console ' > /dev/null; echo "console not loaded yet: $(mod list | grep -c '^console ')"

# most needs a display; asking for one brings the console in
most /etc/hostname 2>/dev/null; echo "most without a terminal rc=$?"
echo "console pulled in: $(mod list | grep -c '^console ')"

# mod avail says which module offers what
mod avail | grep -c 'offers display'

# an interface nothing offers is still refused, and says so
mod drop most > /dev/null
mod drop console > /dev/null
echo "dropped both"
