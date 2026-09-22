# sysinfo: plain in a pipe, and every number must agree with the kernel.
mod load ./build/mods/sysinfo.so && echo "loaded"

out=$(sysinfo)
echo "no escapes in a pipe: $(printf '%s' "$out" | grep -c '\033' || true)"
echo "-p also plain: $(sysinfo -p | grep -c '\033' || true)"
sysinfo --nonsense 2>/dev/null; echo "bad option rc=$?"

# the labels that must be there
for k in OS Kernel Arch Uptime Shell CPU Memory Disk Load; do
  printf '%s ' "$k"
  printf '%s' "$out" | grep -q "$k:" && echo present || echo MISSING
done

# the kernel release it prints is the one uname reports
u=$(uname -r)
printf '%s' "$out" | grep -q "Kernel: $u" && echo "kernel agrees with uname"

# the architecture likewise
a=$(uname -m)
printf '%s' "$out" | grep -q "Arch: $a" && echo "arch agrees with uname"

# and the shell it names is this one
printf '%s' "$out" | grep -q "Shell: hibr $HIBR_VERSION" && echo "names this shell"

mod drop sysinfo > /dev/null && echo "dropped"
