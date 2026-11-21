# 0063 — A declared variable is set

Status: accepted (the owner, 2026-11-20, Gitea #602 item 6)

## Context

bash distinguishes a variable that has attributes from one that has a
value. `declare d`, `typeset d`, `export e`, `readonly r`, `declare -i i`
and `declare -a a` with no value all make a name bash knows about and
still counts as **unset**: `${d-default}` takes the default, `${d+x}` is
empty, `[[ -v d ]]` is false, and `declare -p d` prints `declare -- d`
with no value. Only an assignment, even of the empty string, makes it set.

hibr's variable is a value with attributes beside it, never attributes
waiting for a value: a declaration with no value stores the empty string,
so the name is set from the moment it is declared.

```sh
declare d
echo "[${d-unset}] [${d+set}]"
[[ -v d ]] && echo "-v: set"
declare -p d
```

```output
[] [set]
-v: set
declare -- d=""
```

bash prints `[unset] []`, nothing for the test, and `declare -- d`.

## Decision

A declared variable with no value counts as set, holding the empty string.
This is a deliberate divergence from bash and stays.

- A strict file declares its globals at file level (`declare -g NAME`) and
  then reads them; a name that read as unset until first assigned would
  make every such read a `set -u` failure and every `${NAME-default}` a
  default the file never meant.
- A declared map or array is a real, empty container at once
  (`declare -A m` then `${#m[@]}` is 0 either way), and one rule for every
  kind of declaration beats one for scalars and another for containers.
- A search of `programs/`, `examples/` and the catalogue found no script
  that relies on bash's behaviour: the four desktop files that test a
  declared name with `${X-}` declare it with a value
  (`declare -g X="${X-}"`).

`local x` alone is the one form that does not create the name: it hides
the outer one and leaves this function's unset until assigned, which is
what bash does as well, and what `strict vars` relies on to tell a local
from a global.

## Consequences

A script ported from bash that uses `${x-default}` or `[[ -v x ]]` to ask
whether a `declare x` has been assigned yet gets "set" here; it should
assign a value it can recognise, or test `[[ -n $x ]]`. An element of a
declared scalar is set too, since a scalar is its own element 0
(`declare d; [[ -v d[0] ]]`), and `declare -p` of a declared nameref with
no target prints `declare -n r=""` where bash prints `declare -n r`.

---

[← decisions](README.md)
