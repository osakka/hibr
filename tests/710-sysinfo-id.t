# Which picture a system gets, for every identification worth checking.
# The logic is C, so the check is C: tests/sysinfo-id.c, built here.
out=/tmp/hibr-siid-$$
if gcc -Iinclude -w -o "$out" tests/sysinfo-id.c mods/sysinfo/sysinfo.c src/mem.c 2>/dev/null; then
  "$out"
  rm -f "$out"
else
  echo "no gcc, skipped"
fi
