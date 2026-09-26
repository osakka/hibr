# title moves the environment's strings off the argv region it is about
# to overwrite. It used to replace environ itself with a hibr-owned array,
# which glibc's own setenv/putenv will not trust once environ differs from
# what it manages -- growing it later allocates a fresh array rather than
# realloc or free the one it did not allocate, orphaning ours. Reproduce
# that class of bug with ASAN_OPTIONS=detect_leaks=1 ./build/hibr.asan
# tests/840-title-environ.t: only a build with LeakSanitizer catches it,
# so this file's own recorded comparison just checks title and export
# still work together, correctly, in either order.
title hibr-worker
export HIBR_T840=1
echo "$HIBR_T840"
unset HIBR_T840
export HIBR_T840=2
echo "$HIBR_T840"
