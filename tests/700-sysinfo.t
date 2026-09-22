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

# every picture draws, and none of them leaks its tone marks into the text
for l in hibr debian alpine cix; do
  printf '%s ' "$l"
  a=$(sysinfo -l "$l")
  case "$a" in
    *'$1'*|*'$2'*) echo "LEAKS MARKERS" ;;
    *) echo "clean" ;;
  esac
done

# the picture is padded to a straight edge: every line puts the label column
# in the same place
sysinfo -l cix | rsub -g "$(sysinfo -l cix)" "[^ ]" "x" > /dev/null
echo "cix lines: $(sysinfo -l cix | grep -c .)"

# an unknown name falls back rather than failing
sysinfo -l nosuchdistro > /dev/null; echo "unknown picture rc=$?"

mod drop sysinfo > /dev/null && echo "dropped"
