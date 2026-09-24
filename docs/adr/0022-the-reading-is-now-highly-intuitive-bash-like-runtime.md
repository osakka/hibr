# 0022 — The reading is now Highly Intuitive Bash-like Runtime

Status: accepted

Supersedes: [0015](0015-the-name-is-hibr.md)

## Context

0015 read **hibr** as Highly Improved Bash Runtime. Two words in that reading
had drifted from how the project actually talks about itself by the time this
was written.

*Improved* implies a baseline anyone would already agree needs improving.
What the traps recorded throughout this project actually are is *surprising*
rather than *bad* — `test`'s word-splitting, `$BASH_REMATCH`'s capture name,
a dozen more each collected because someone tripped on it, not because bash
was judged wanting in the abstract. *Intuitive* names the result of fixing
them without claiming the standing to have judged bash in the first place:
after enough of those traps are gone, what is left behaves the way a reader
would expect on the first try.

*Bash Runtime*, said plainly, reads as a claim to run bash — the exact
confusion the README already spends a paragraph heading off, and the reason
CLAUDE.md says outright that hibr "is not a drop-in replacement for bash and
must not be described as one." *Bash-like* says the same thing the
disclaimer does, in the name itself, so the name stops needing the
disclaimer to be read correctly.

## Decision

The name is still **hibr**; only the four words behind the letters change, to
**H**ighly **I**ntuitive **B**ash-like **R**untime.

*Highly* is unchanged — it still names the numbers in the README's own
measurements table. *Runtime* is unchanged — it still carries the module
ABI, results without forking, and everything else 0015 already said under
that word. *Intuitive* and *Bash-like* replace *Improved* and *Bash*, for
the reasons above: one is a truer description of what fixing a sharp edge
actually buys a reader, and the other is an accurate claim instead of one
that needs a footnote.

## Consequences

Every place the old reading appeared in prose -- the README, the About hibr
window, this directory's own index -- says the new one instead. The ADR
history does not: 0015 keeps its original text and reasoning, marked
superseded, because it is a record of a decision made at the time, not a
draft of this one.

---

[← decisions](README.md)
