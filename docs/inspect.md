# Source inspection, interface version 1

This is the authoritative contract for Hibr's `inspect` builtin and versioned C interface. ADR 0056 records the design rationale. Version 1 is pending the provider's reviewed release; it is not frozen by this document alone.

## Requests and results

Load the module with `need inspect`. Requests are `functions FILE`, `calls FILE [FUNCTION]`, `refs FILE` and `check FILE`. Unbound requests render a human summary and all evidence fields. `-j` prints JSON instead. A bound request (`r := inspect ...`) is quiet and returns the same data as a typed map, including when `-j` is present. Exit status is zero for ok/empty input, nonzero for invalid, incomplete, unreadable or unsupported requests. Capture a failed bound request inside `if` when using strict error handling.

- `functions` reports definitions, declared parameters, preceding author comments and body locations.
- `calls` reports command words and command substitutions. An optional function name filters by enclosing function. It classifies names against this file's definitions, core builtins and already loaded modules; everything else stays unresolved.
- `refs` reports literal arguments naming functions defined in this file. `--symbols JSON_ARRAY` additionally matches caller-supplied literal candidates. Such candidates remain unresolved; they do not establish cross-file execution or resolution.
- `check` reports parser diagnostics and findings from the same implementation as `--explain`, with stable rule identifiers.

```sh
need inspect
cat > sample.hibr <<'SOURCE'
# Return the declared value.
fn sample(int value=3) { ret "$value"; }
SOURCE
r := inspect functions sample.hibr
printf '%s: %s = %s\n' "${r[functions][0][name]}" "${r[functions][0][params][0][type]}" "${r[functions][0][params][0][default][text]}"
```
```output
sample: int = 3
```

```sh
need inspect
printf 'fn sample() { top := foreign_command; }\n' > sample.hibr
r := inspect calls sample.hibr sample -j
printf '%s %s bound=%s target=%s\n' "${r[calls][0][name]}" "${r[calls][0][resolution]}" "${r[calls][0][bound]}" "${r[calls][0][binding]}"
```
```output
foreign_command unresolved bound=true target=top
```

## Record vocabulary

The result contains `inspect: 1`, `state`, `partial`, `source`, the requested array (`functions`, `calls`, `refs`, or `findings`), and `diagnostics`. An unsupported request has `state: unsupported` and diagnostics, without claiming source inspection.

`state` is `ok`, `empty` (zero source bytes), `incomplete`, `invalid`, or `unreadable`. `partial` means syntax damage or a substitution that could not be fully analysed; an empty array is never proof of no runtime behavior. Valid top-level units survive recovery after invalid syntax. Definitions are discovered statically, including those inside conditional branches; inspection does not claim they ran.

Every evidence record has `source`, `span` and `known`. Nested body metadata inherits its enclosing source; unevaluated defaults and author summaries include their own evidence provenance.

| Field | Meaning |
| --- | --- |
| `source.path`, `.size`, `.id` | Path as requested, bytes read, and `fnv1a64:` plus 16 hex digits. Size/id are null if unavailable. Identity describes the exact inspected bytes, not later file contents. |
| `span.start`, `.end` | Byte offsets, start inclusive and end exclusive. |
| `span.line`, `.col`, `.endline` | One-based line and byte column. `endline` is the **last covered line**, using `end - 1` for a non-empty span and the start line for an empty span. |
| `known` | `syntax` is seen in code; `declared` is author intent/signatures; `inferred` cites a rule. `observed` is reserved for later runtime evidence and is never emitted here. |
| `name`, `owner`, `scope` | Definition/command/reference name, enclosing function (empty at top level), and `substitution` scope where applicable. An unavailable command name is empty or null; inspect its dynamic/reason fields. |
| `line` | Additional original parser line metadata where available. Use `span` for source navigation. |
| `params`, `declared_signature` | Declared parameter records and whether the definition uses `fn` syntax. Each parameter carries name, type (empty if untyped), rest, has_default and default. |
| `default` | Null when absent; otherwise declared source evidence with original `text` and `evaluated: false`. Defaults are never evaluated. |
| `summary`, `body` | Preceding author comment evidence and body span. A comment describes intent, not proven behavior. Body bytes are not returned. |
| `resolution` | `function`, `builtin`, `module` or `unresolved`. A supplied external reference stays unresolved. |
| `bound`, `binding`, `binding_source` | Whether `:=` binds a call, its static target when known, or a computed target's source span. |
| `dynamic`, `expression`, `expression_state`, `reason` | An expanded command name remains dynamic/unresolved. Its expression is shown unless it contains command code; then source-span-required replaces disclosure. Unmapped transformed backticks and depth-limited fragments are explicitly unresolved/partial. |
| `lookup` | References distinguish `inspected-file` from `supplied-symbol`. Literal references prove naming, not calling. |
| `registration`, `rule`, `rules_path`, `rules_source` | Optional convention inference, its named rule and exact convention-file identity. Registration records also identify prefix and role. |
| `message`, `reference` | Diagnostic/finding text and, for lint findings, the supporting guideline reference. |

## Options and limits

`--rules FILE` reads an explicit JSON convention file (the desktop uses `programs/desktop/conventions.json`). Its role registrations and callback IDs produce **inferred** edges, never language facts. Both structural tests and inspection consume that file.

`--symbols JSON_ARRAY` accepts at most **4,096** identifier strings of at most **128 bytes each**. Empty lists are accepted. Wrong types, invalid identifiers and exceeded limits are refused with `state: unreadable`, a diagnostic and no facts. Names are supplied by the caller, matched literally and reported unresolved. There is no source search or cross-file loading.

Sources and rules are limited to **4 MiB**, regular files or registered local schemes only. Non-local schemes, NUL and invalid UTF-8 are refused. Options JSON is limited to **1 MiB**. The core parser's depth bound applies, with an additional substitution-analysis depth bound of **32**. Malformed settings are unreadable, never silently broadened. Diagnostics stay in the result, not stderr or desktop.log.

```sh
need inspect
printf 'dt_ticker external_callback\n' > sample.hibr
r := inspect refs sample.hibr --symbols '["external_callback"]'
printf '%s %s %s\n' "${r[refs][0][name]}" "${r[refs][0][lookup]}" "${r[refs][0][resolution]}"
```
```output
external_callback supplied-symbol unresolved
```

```sh
need inspect
printf ':\n' > sample.hibr
if r := inspect refs sample.hibr --symbols '[42]'; then :; fi
printf '%s: %s\n' "${r[state]}" "${r[diagnostics][0][message]}"
```
```output
unreadable: symbols must be an array of identifier strings
```

```sh
need inspect
printf 'fn unfinished(' > sample.hibr
if r := inspect functions sample.hibr; then :; fi
printf '%s partial=%s\n' "${r[state]}" "${r[partial]}"
```
```output
incomplete partial=true
```

```sh
need inspect
: > empty.hibr
inspect check empty.hibr -j
```
```output
{"inspect":1,"state":"empty","partial":false,"source":{"path":"empty.hibr","size":0,"id":"fnv1a64:cbf29ce484222325"},"findings":[],"diagnostics":[]}
```

```sh
need inspect
printf 'fn sample_draw() { :; }\nregister sample\n' > sample.hibr
printf '%s\n' '{"roles":{"window":{"registrations":{"register":1},"callbacks":{"draw":{"arity":0,"id":"window.draw"}}}}}' > rules.json
r := inspect calls sample.hibr --rules rules.json
printf '%s %s %s\n' "${r[calls][1][name]}" "${r[calls][1][known]}" "${r[calls][1][rule]}"
```
```output
sample_draw inferred window.draw
```

## Safety and C consumers

Nothing in inspected source runs: no commands, substitutions, imports, aliases, traps or module initialization. Classification never scans the autoload index. Source identity and explicit uncertainty must accompany any explanation. Privacy defaults to names, locations, signatures and author comments; unrelated argument strings and body text are not disclosed. Signatures can contain sensitive defaults or comments: choosing to inspect does not authorize sending records elsewhere.

C consumers include `hibr.h` and installed `hibr/inspect.h`, then request `hibr_require(s, "inspect", 1)`. The `ix_api.query(caller, request, path, options, out)` function appends JSON to an initialized, caller-owned `str`. Options is null or JSON containing optional `function`, `rules` and `symbols` fields with the same meanings and limits. The consumer frees the string. No parser pointers escape. C queries preserve caller status, RET and line state; the builtin uses the normal result slot. Interface version matching uses Hibr's existing provide/require mechanism, independently of public AST layout (there is none).

These examples are executed by `tests/531-doc-examples.t`. `make check-inspect` covers behavior, bounds, provenance, non-execution, recovery, all tracked source files and function extraction round trips. The sanitizer gate and inspection fuzzer include symbol-limit cases. Review and Hibr's release gate precede freezing the interface.
