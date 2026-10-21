# DECCRA, the rectangle copy (Gitea #337): CSI Pts;Pls;Pbs;Prs;Pps;Ptd;Pld;Ppd $ v.
# 1-based and inclusive, the pages ignored, clipped to the screen, the source
# read whole before anything is written so an overlap copies what was there,
# and a wide glyph cut by an edge of the destination blanked on both halves.
# The cursor and the pen do not move. Recorded, since the module makes its own
# terminal and no other shell has one.

mod load ./build/mods/pty.so
mod load ./build/mods/term.so
E=$'\e'
fresh() { t := term new -r 8 -c 20; }
show() { local i=0 r; while [ "$i" -lt 8 ]; do r := term row "$t" "$i"; printf '%d|%s|\n' "$i" "$r"; i=$((i + 1)); done; }

echo "-- a copy, and the source left as it was"
fresh
term feed "$t" "${E}[1;1Habcdef${E}[1;1;1;3;1;3;5;1\$v"
show
echo "-- overlapping: the source as it was before the copy"
fresh
term feed "$t" "${E}[2;1Habcdefgh${E}[2;1;2;6;1;2;3;1\$v"
show
echo "-- clipped where the destination runs off the screen"
fresh
term feed "$t" "${E}[3;1H0123456789${E}[3;1;3;10;1;4;15;1\$v"
show
echo "-- a wide glyph cut by the destination's edges is blanked"
fresh
term feed "$t" "${E}[1;1H漢字漢字${E}[1;2;1;5;1;5;1;1\$v"
show
echo "-- a block of rows, and the cursor where it was"
fresh
term feed "$t" "${E}[1;1Haa${E}[2;1Hbb${E}[3;1Hcc${E}[7;10H${E}[1;1;3;2;1;4;8;1\$vX"
show
echo "-- origin mode counts from the top margin"
fresh
term feed "$t" "${E}[3;6r${E}[?6h${E}[1;1Hmm${E}[1;1;1;2;1;3;5;1\$v"
show
