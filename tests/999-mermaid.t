# mermaid: the three kinds of diagram this release understands, drawn into
# cells, and the things it says plainly that it does not understand. Mermaid
# has no conformance suite to run against, so the check is a corpus of
# diagrams with their drawing recorded -- the way tests/md_spec.py holds
# CommonMark's examples -- and tests/mermaid.py asserts the properties a
# drawing must have whatever it looks like.
mod load ./build/mods/mermaid.so && echo "loaded"

for f in flow wide lr shapes amp seq pie; do
  echo "== $f"
  mermaid render -w 44 "tests/mermaid/$f.mmd"
done

echo "== the same flowchart in the ascii set"
mermaid render -a tests/mermaid/flow.mmd

echo "== the graph itself, rather than its drawing"
mermaid parse tests/mermaid/flow.mmd

echo "== what it says it cannot do"
mermaid render -t "classDiagram
  class A" 2>&1; echo "rc=$?"
mermaid render -t "flowchart TD
  subgraph one
  A --> B
  end" 2>&1; echo "rc=$?"
mermaid render -t "flowchart TD
  A[[subroutine]] --> B" 2>&1; echo "rc=$?"
mermaid render -t "sequenceDiagram
  loop every day
  A->>B: hi
  end" 2>&1; echo "rc=$?"
mermaid render -t "flowchart TD
  A ~~> B" 2>&1; echo "rc=$?"
mermaid render -t "" 2>&1; echo "rc=$?"
mermaid render -t "pie
  bad line" 2>&1; echo "rc=$?"

echo "== a self-loop is drawn without the loop, and says so"
mermaid render -t "flowchart TD
  A --> B
  B --> B" 2>&1

echo "== with := it fills the slot instead of printing"
d := mermaid render -t "flowchart LR
  A[hi] --> B[there]"
echo "kind=${d["kind"]} w=${d["w"]} h=${d["h"]} rows=${#d["text"][@]}"
i=0
while [ "$i" -lt "${#d["text"][@]}" ]; do
  printf '%s\n     %s\n' "${d["text"][$i]}" "${d["runs"][$i]}"
  i=$((i + 1))
done
e := mermaid render -t "nonsense"
echo "kind='${e["kind"]}' why=${e["why"]}"

echo "== parse does not fill a slot, and says which one does"
x := mermaid parse -t "flowchart TD
  A --> B" 2>&1; echo "rc=$?"

mod drop mermaid > /dev/null && echo "dropped"
