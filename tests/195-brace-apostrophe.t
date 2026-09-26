# A bare apostrophe in a ${...} default/alternate/pattern's own text is
# just a character, not a quote -- even real bash fails to parse this
# (confirmed: "unexpected EOF while looking for matching `''"), so this
# is a deliberate divergence, not a bash-compatibility bug: nothing that
# runs in bash could have relied on the broken form, so accepting more
# here costs nothing. Recorded, not compared against bash, since bash
# itself cannot run this file. See docs/adr and CLAUDE.md.

echo "${FOO:-the machine's zone}"
FOO=set
echo "${FOO:-the machine's zone}"
unset FOO
echo "${FOO-it's a test}"
unset FOO
echo "[${FOO:+it's alt}]"
FOO=x
echo "${FOO:+it's alt}"

# The fix only silences the apostrophe-opens-a-quote check; a glob
# character in the same pattern still globs, and a literal apostrophe in
# the pattern still matches literally, side by side.
v="cat's toy"
echo "${v#*'s }"
w=hello.tar.gz
echo "${w%.tar.gz} ${w#*.} ${w/l/L}"

# A real apostrophe outside any ${...}, in the same double-quoted string,
# already worked and must keep working.
FOO=x
echo "plain ${FOO} it's ok"

# An actual single-quoted string, unrelated to any of this, is unaffected.
echo 'it'\''s ok'
