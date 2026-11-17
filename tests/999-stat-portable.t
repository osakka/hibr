# A file's times are st_mtim on Linux and st_mtimespec on macOS, and nothing
# here compiles for macOS before a person runs `brew upgrade`: 0.99.535's
# tray-icon cache read st.st_mtim in mods/img/img.c, and every Mac build
# from 0.99.535 to 0.99.544 failed there (0.99.545). So a source file that
# names st_atim, st_mtim or st_ctim must also say what macOS calls it --
# `#define st_mtim st_mtimespec`, its own __APPLE__ branch -- or use
# HIBR_MTNS from include/hibr.h. This names each that does neither.
[ -d ./src ] && [ -d ./mods ] || { echo "every file that reads a file's time says what macOS calls it"; exit 0; }
bad=0
for f in ./src/*.c ./mods/*.c ./mods/*/*.c; do
	grep -qE 'st_[amc]tim\b' "$f" || continue
	grep -qE 'st_[amc]timespec' "$f" && continue
	echo "Linux-only: $f"
	bad=1
done
[ "$bad" = 0 ] && echo "every file that reads a file's time says what macOS calls it"
exit 0
