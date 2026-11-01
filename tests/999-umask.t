# umask takes a symbolic mode and -S and -p as bash does. Until 0.99.417 it
# read every argument as octal with no check, so `umask -S` -- which only
# shows the mask -- and `umask u=rwx,g=rx,o=` both set it to 0000, and every
# file made after was writable by anyone.
d=$(mktemp -d) || exit 1
umask 022; umask; umask -S; umask -p; umask -p -S
umask u=rwx,g=rx,o=; umask; umask -S
umask a-w; umask
umask g+w; umask
umask o=r,g=; umask
umask 077; umask -S
umask -S u=rwx,go=rx
umask 9; echo "st $?"; umask
umask 0800; echo "st $?"
umask u=q; echo "st $?"; umask
umask uq; echo "st $?"; umask
umask u+r-w; umask
umask -- 027; umask
umask 022; umask -S > /dev/null; : > "$d/a"; stat -c %a "$d/a"
umask u=rwx,g=rx,o=; : > "$d/b"; stat -c %a "$d/b"
rm -rf "$d"
