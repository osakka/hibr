# contain refuses rather than run a command uncontained (Gitea #432,
# ADR 0052). What a kernel offers differs from one machine to the next, so
# only what is the same everywhere is recorded here: the usage, a folder
# that is not there, and no command. Whether the containment holds is
# tests/contain.py's, run where the kernel has Landlock.
mod load ./build/mods/contain.so
contain 2> /dev/null; echo "no verb: $?"
contain exec 2> /dev/null; echo "no folder: $?"
contain exec /nonexistent/folder -- true 2> /dev/null; echo "no such folder: $?"
contain exec . -- 2> /dev/null; echo "no command: $?"
contain probe > /dev/null 2>&1; case $? in 0 | 1) echo "probe answers 0 or 1" ;; *) echo "probe answered $?" ;; esac
p := contain probe; case $p in '' | landlock\ [0-9]* | sandbox) echo "probe binds" ;; *) echo "probe bound: $p" ;; esac
