# A key cap's bevel is two pen names, framed-raised and framed-pressed
# (SGR 51:6 and 51:7, Blit ADR 0237, Gitea #604), set with the dark edge's
# line colour; the console needs no screen to take a pen.
mod load ./build/mods/console.so
console pen "#cbd5e0" "#2d3748" framed-raised "line=#0a0e14" && echo "framed-raised taken"
console pen "#cbd5e0" "#2d3748" framed-pressed "line=#0a0e14" && echo "framed-pressed taken"
console pen "#cbd5e0" "#2d3748" framed-sunken 2> /dev/null || echo "an unknown kind refused"
