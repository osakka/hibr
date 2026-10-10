# `source` honours a scheme whose module says it reads something already on
# this machine, so an app's code can come out of a bundle the archive module
# has mounted -- which `source` could not do before 0.99.140, because it
# opens its file itself and a scheme is honoured by rd_do, the one place a
# redirection opens anything (ADR 0043, Gitea #159's groundwork).
#
# The gate is that flag and nothing else. The http module registers a scheme
# too, so honouring every scheme would have made `source` a one-word way to
# fetch and run code off the network -- which is what the first version of
# this release did, measured, and is why the check below is here: against
# that version it connects (`connect 127.0.0.1:9: Connection refused`) and
# says "the scheme did not open it" instead.
h=$HIBR
case $h in /*) ;; *) h=$PWD/$h ;; esac
d=$(mktemp -d)
mod load ./build/mods/archive.so
mod load ./build/mods/http.so
printf 'greet() { echo "sourced out of a bundle"; }\n' > "$d/app.hibr"
printf 'echo "inside: BASH_SOURCE=$BASH_SOURCE"\n' >> "$d/app.hibr"
printf 'a resource\n' > "$d/pic.txt"
tar -czf "$d/b.tar.gz" -C "$d" app.hibr pic.txt
archive open b "$d/b.tar.gz"

echo "--- a resource through the scheme, which has always worked:"
read -r l < /dev/archive/b/pic.txt
echo "$l"

echo "--- and now the code, which is what this adds:"
. /dev/archive/b/app.hibr
echo "status $?"
greet

echo "--- a scheme that has not said it is local is refused by name:"
. /dev/http/127.0.0.1/9/x.hibr
echo "status $?"

echo "--- and a socket is not something source may open at all:"
. /dev/tcp/127.0.0.1/9 2> /dev/null
echo "status $?"

# A dry run cannot reach the scheme at all, and that is the point rather
# than a gap: `mod` and `need` both refuse under a plan, so nothing ever
# registers one there. What is asserted is the outcome -- nothing is opened
# and nothing inside runs -- not the wording of the refusal.
echo "--- and a dry run does not open one and run what is inside:"
"$h" --plan -c '. /dev/archive/b/app.hibr && echo RAN' 2>&1
rm -rf "$d"
