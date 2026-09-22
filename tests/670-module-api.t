# One module offering a table of functions to another. Modules are opened
# RTLD_LOCAL on purpose, so this registry is the only way across.
mod load ./build/mods/most.so && echo "most loads on its own"

# ... but it cannot work until something offers it a screen
most /etc/hostname 2>/dev/null; echo "most without screen rc=$?"

mod load ./build/mods/screen.so && echo "screen loads and offers its table"

# dropping the provider withdraws the offer
mod drop screen > /dev/null && echo "screen dropped"
most /etc/hostname 2>/dev/null; echo "most after the offer is withdrawn rc=$?"

mod drop most > /dev/null && echo "most dropped"
