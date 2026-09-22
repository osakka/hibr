# One module offering a table of functions to another. Modules are opened
# RTLD_LOCAL on purpose, so this registry is the only way across.
mod load ./build/mods/most.so && echo "most loads on its own"

# ... but it cannot work until something offers it a screen
most /etc/hostname 2>/dev/null; echo "most without a display rc=$?"

mod load ./build/mods/console.so && echo "console loads and offers a display"

# dropping the provider withdraws the offer
mod drop console > /dev/null && echo "console dropped"
most /etc/hostname 2>/dev/null; echo "most after the offer is withdrawn rc=$?"

mod drop most > /dev/null && echo "most dropped"
