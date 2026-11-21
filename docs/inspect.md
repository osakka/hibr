# Source inspection, interface version 1

This is the authoritative contract for Hibr's `inspect` builtin and versioned C interface. ADR 0056 records the design rationale. Version 1 is pending the provider's reviewed release; it is not frozen by this document alone.

## Requests and results

Load the module with `need inspect`. Requests are `functions FILE`, `calls FILE [FUNCTION]`, `refs FILE`, `vars FILE [FUNCTION]` and `check FILE`. Unbound requests render a human summary and all evidence fields. `-j` prints JSON instead. A bound request (`r := inspect ...`) is quiet and returns the same data as a typed map, including when `-j` is present. Exit status is zero for ok/empty input, nonzero for invalid, incomplete, unreadable or unsupported requests. Capture a failed bound request inside `if` when using strict error handling.

- `functions` reports definitions, declared parameters, preceding author comments and body locations.
- `calls` reports command words and command substitutions. An optional function name filters by enclosing function. It classifies names against this file's definitions, core builtins and already loaded modules; everything else stays unresolved. `--arguments JSON_ARRAY` names commands whose calls also carry their `arguments`; see [Call arguments](#call-arguments).
- `refs` reports literal arguments naming functions defined in this file. `--symbols JSON_ARRAY` additionally matches caller-supplied literal candidates. Such candidates remain unresolved; they do not establish cross-file execution or resolution.
- `vars` reports every variable the parser sees read, written or named, one record per access, with its form, subscripts, enclosing function and whether that function declares the name. An optional function name filters by enclosing function. See [Variable facts](#variable-facts).
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

The result contains `inspect: 1`, `state`, `partial`, `source`, the requested array (`functions`, `calls`, `refs`, `vars`, or `findings`), and `diagnostics`. An unsupported request has `state: unsupported` and diagnostics, without claiming source inspection.

Requests are added within an interface version; the number moves only when a request, field or value already documented changes meaning or goes. A consumer that needs a later request asks for it and treats `state: unsupported` as the producer not offering it: `vars` arrived this way, so a producer from before it answers `unsupported` for `vars` and everything else as it did.

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
| `tested` | Whether the call's exit status is tested where it stands, as the shell tests it: the condition of `if`, `while` or `until`, the left of `&&` or `||`, what `!` negates, or the last command of a group, subshell, pipeline or `time` that is itself tested. A function body starts untested, and a status read later through `$?` is not seen. |
| `dynamic`, `expression`, `expression_state`, `reason` | An expanded command name remains dynamic/unresolved. Its expression is shown unless it contains command code; then source-span-required replaces disclosure. Unmapped transformed backticks and depth-limited fragments are explicitly unresolved/partial. |
| `lookup` | References distinguish `inspected-file` from `supplied-symbol`. Literal references prove naming, not calling. |
| `registration`, `rule`, `rules_path`, `rules_source` | Optional convention inference, its named rule and exact convention-file identity. Registration records also identify prefix and role. |
| `message`, `reference` | Diagnostic/finding text and, for lint findings, the supporting guideline reference. |
| `access`, `form` | For `vars`: `read`, `write` or `name`, and the syntax the access is written in (see [Variable facts](#variable-facts)). |
| `declared` | For `vars`: `param` or `local` when the enclosing function declares the name as a parameter or with `local` (or `declare`/`typeset` without `-g`) anywhere in its own body; otherwise null. |
| `exact`, `lexical` | For `vars`: whether the span covers the access's own bytes, and whether the fact came from the lexical scan of arithmetic text. |
| `subscripts`, `keys` | For `vars`: how many subscripts the access is written with, and the first two as `text` and `quote` (`quoted`, `unquoted` or `dynamic`). |
| `verb`, `callee` | For `vars`: the operation of an `arr`, `json` or `str` access, and the command a named argument is given to. |
| `arguments` | For `calls` to a command named with `--arguments`: one record per argument word, in order, with `text`, `quote` and `span` (see [Call arguments](#call-arguments)). Absent on every other call. |

## Variable facts

`vars` reads the same parse as every other request and runs nothing. Each record names one variable access in the inspected file:

```sh
need inspect
cat > sample.hibr <<'SOURCE'
fn note(id) {
	local msg=${NOTE_TEXT[$id]}
	NOTES["$id"]+=$msg
	(( NOTE_COUNT++ ))
}
SOURCE
r := inspect vars sample.hibr
for i in "${!r[vars][@]}"; do
	printf '%s %s %s line=%s declared=%s\n' "${r[vars][$i][name]}" "${r[vars][$i][access]}" \
		"${r[vars][$i][form]}" "${r[vars][$i][span][line]}" "${r[vars][$i][declared]}"
done
printf '%s: %s %s\n' "${r[vars][4][name]}" "${r[vars][4][keys][0][quote]}" "${r[vars][4][keys][0][text]}"
printf '%s lexical=%s %s\n' "${r[vars][7][name]}" "${r[vars][7][lexical]}" "${r[vars][7][rule]}"
```
```output
id write param line=1 declared=param
msg write local line=2 declared=local
NOTE_TEXT read expand line=2 declared=null
id read expand line=2 declared=param
NOTES write append line=3 declared=null
id read expand line=3 declared=param
msg read expand line=3 declared=local
NOTE_COUNT write arithmetic line=4 declared=null
NOTES: dynamic "$id"
NOTE_COUNT lexical=true arithmetic-text
```

Records come in source order. Every one carries `source`, `span`, `known`, `exact`, `name`, `access`, `form`, `owner` (the enclosing function, empty at top level), `declared`, `lexical`, `subscripts` and `keys`; `scope: substitution` marks one found inside `$( )`, a backquote or a process substitution:

```sh
need inspect
printf 'TOTAL+=1\n' > sample.hibr
inspect vars sample.hibr -j
```
```output
{"inspect":1,"state":"ok","partial":false,"source":{"path":"sample.hibr","size":9,"id":"fnv1a64:e7895b4e401d6e1a"},"vars":[{"source":{"path":"sample.hibr","size":9,"id":"fnv1a64:e7895b4e401d6e1a"},"known":"syntax","span":{"start":0,"end":5,"line":1,"col":1,"endline":1},"exact":true,"name":"TOTAL","access":"write","form":"append","owner":"","declared":null,"lexical":false,"subscripts":0,"keys":[]}],"diagnostics":[]}
```

| form | access | written as |
| --- | --- | --- |
| `assign`, `append` | write | `name=v`, `name[k]=v`, `name+=v` as a command of its own |
| `prefix` | write | `name=v cmd`, an assignment for one command |
| `array`, `array-append`, `array-key` | write | `name=(...)`, `name+=(...)`, and one record per `[k]=v` element |
| `bind` | write | `name := cmd` and `name[k] := cmd` |
| `local`, `declare`, `typeset`, `export`, `readonly` | write (read for `-p`) | a declaration's names and array clauses; a type word such as `int` before the first name is not one, as the shell reads it |
| `nameref` | write, name | `declare -n ref=TARGET`: the reference is written and the target named |
| `param` | write | a declared parameter of `fn name(...)`, a rest parameter `...name` included; the span is the parameter as declared |
| `unset`, `read`, `read-array`, `mapfile`, `printf`, `getopts`, `match`, `rsub` | write | the names these builtins write, flags and their operands skipped (`readarray` is `mapfile`) |
| `arr`, `json`, `str` | read, write | the variable an `arr`, `json` or `str` operation reads or writes by name, with its `verb`; one that changes a variable in place gives both |
| `test` | read | `[[ -v name ]]`, `[ -v name ]`, `test -v name` |
| `for`, `select` | write | the loop variable |
| `fd-variable` | write (read for a close) | `{name}>file` and its kin; `{name}>&-` closes the descriptor the variable holds |
| `expand`, `length`, `keys`, `indirect`, `names` | read | `$x` and `${x...}`, `${#x}`, `${!x[@]}`, `${!x}`, `${!prefix@}` (the name is the prefix) |
| `default-assign` | write | `${x:=word}`, beside its `expand` read |
| `arithmetic` | read, write | a name in `(( ))`, `$(( ))`, `$[ ]`, `for (( ))`, `let` or a `${x:offset:length}`; written when an assignment operator, `++` or `--` follows or `++`/`--` precedes it |
| `argument` | name | a literal argument naming a variable, below |

`known` is `syntax` for everything the parser marks. Arithmetic text has no parse tree until it runs, so the names in it are found by a lexical scan of the parser-delimited text: those records are `known: inferred`, `rule: arithmetic-text`, `lexical: true`. The `$` expansions inside arithmetic are lexed as words and are ordinary `expand` records.

`exact` is false when the parser recorded no bytes for the access itself; the span is then the enclosing word or command. Here-document bodies are the common case: they are read from a copy, so a `$name` in one points at the command that opened it. Every other form gets its own bytes, checked against the source before they are given.

A literal argument is a variable's name in two cases. When a function in the same file takes that argument by name -- as the value of `local -n`/`declare -n`, as an indirect `${!param}`, or as the name an `unset`, `read`, `printf -v`, `arr`, `json` or similar builtin writes through `"$param"` -- the call's argument at that position is `access: name`, `known: inferred`, `rule: by-name-parameter`, with `callee`. Otherwise only a name supplied with `--symbols` is reported, `lookup: supplied-symbol`, as `refs` does: other argument text stays private.

```sh
need inspect
cat > sample.hibr <<'SOURCE'
fn keep(name) { local -n slot=$1; slot=1; }
keep SAVED_STATE
dt_keep KEPT_STATE
SOURCE
r := inspect vars sample.hibr --symbols '["KEPT_STATE"]'
for i in "${!r[vars][@]}"; do
	[ "${r[vars][$i][access]}" = name ] || continue
	printf '%s to %s: %s %s%s\n' "${r[vars][$i][name]}" "${r[vars][$i][callee]}" "${r[vars][$i][known]}" \
		"${r[vars][$i][rule]}" "${r[vars][$i][lookup]}"
done
```
```output
SAVED_STATE to keep: inferred by-name-parameter
KEPT_STATE to dt_keep: syntax supplied-symbol
```

A subscript's `text` is its literal value, quotes removed, when it is `quoted` or `unquoted`; a `dynamic` one gives its source as written, or null when it holds command code. An unquoted literal subscript of an array not declared `-A` is arithmetic when the script runs, so a bare name there may read that variable; it is reported as the key `unquoted`, not as a read.

`declared` is lexical: it says what the enclosing function declares, not what is in scope at run time. A function a caller's `local` reaches through dynamic scope is not followed, nor is anything across files. Variables the shell sets implicitly (`REPLY`, `M`, `BASH_REMATCH`, `RET`, `OPTARG`, `OPTIND`, `PWD`), positional and special parameters, the names a by-name builtin is handed through a variable whose value is unknown, the text inside quoted strings that another command may later run (`eval`, `trap`, `sh -c`, `arr map`), and a name arithmetic would read through the value of another variable are not reported.

## Call arguments

A call's arguments are its own text, so `calls` gives none by default. A consumer that needs them names the commands it means with `--arguments`, and only calls to those commands carry an `arguments` array: a registration's prefix, a menu's title, the name a guard asks about. Each argument word is one record, in order. `text` is its value with quotes removed when the word is all literal text, and `quote` is then `quoted` (every part was quoted) or `unquoted`; a word that expands is `dynamic`, and its `text` is its source as written, or null when it holds command code. `span` covers the word as written, or is null when the parser recorded none. Nothing is evaluated: `$DT_HERE` stays the text `$DT_HERE`.

```sh
need inspect
cat > sample.hibr <<'SOURCE'
fn sample_menus() {
	dt_menu "File"
	. "$DT_HERE/lib/pim.hibr"
	echo "not asked for"
}
SOURCE
r := inspect calls sample.hibr --arguments '["dt_menu", "."]'
for i in "${!r[calls][@]}"; do
	printf '%s:' "${r[calls][$i][name]}"
	for j in "${!r[calls][$i][arguments][@]}"; do
		printf ' %s (%s)' "${r[calls][$i][arguments][$j][text]}" "${r[calls][$i][arguments][$j][quote]}"
	done
	echo
done
```
```output
dt_menu: File (quoted)
.: "$DT_HERE/lib/pim.hibr" (dynamic)
echo:
```

`--arguments` was added within version 1, and a producer from before it does not refuse it: the builtin takes the unknown word for a function name and finds no calls, and a C query ignores the unknown field and gives calls with no `arguments`. A consumer that asks for them therefore checks that a call to a command it named carries `arguments`, and that it found calls at all, rather than reading their absence as no arguments.

`--symbols` and `--arguments` answer different questions: symbols are values to look for among literal arguments, reported as `refs`; arguments are the commands whose argument words a consumer wants on their `calls` records.

## Options and limits

`--rules FILE` reads an explicit JSON convention file (the desktop uses `programs/desktop/conventions.json`). Its role registrations and callback IDs produce **inferred** edges, never language facts. Both structural tests and inspection consume that file.

`--symbols JSON_ARRAY` accepts at most **4,096** identifier strings of at most **128 bytes each**. Empty lists are accepted. Wrong types, invalid identifiers and exceeded limits are refused with `state: unreadable`, a diagnostic and no facts. Names are supplied by the caller, matched literally and reported unresolved. There is no source search or cross-file loading.

`--arguments JSON_ARRAY` accepts at most **4,096** command names of at most **128 bytes each**, each non-empty with no blank or control character (`.` is a command name). It is refused the same way.

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

Nothing in inspected source runs: no commands, substitutions, imports, aliases, traps or module initialization. Classification never scans the autoload index. Source identity and explicit uncertainty must accompany any explanation. Privacy defaults to names, locations, signatures and author comments; argument strings are disclosed only for the commands a caller names with `--arguments`, and body text is not disclosed. Signatures can contain sensitive defaults or comments: choosing to inspect does not authorize sending records elsewhere.

C consumers include `hibr.h` and installed `hibr/inspect.h`, then request `hibr_require(s, "inspect", 1)`. The `ix_api.query(caller, request, path, options, out)` function appends JSON to an initialized, caller-owned `str`. Requests are the same names as the builtin's (`functions`, `calls`, `refs`, `vars`, `check`). Options is null or JSON containing optional `function`, `rules`, `symbols` and `arguments` fields with the same meanings and limits. The consumer frees the string. No parser pointers escape. C queries preserve caller status, RET and line state; the builtin uses the normal result slot. Interface version matching uses Hibr's existing provide/require mechanism, independently of public AST layout (there is none).

These examples are executed by `tests/531-doc-examples.t`. `make check-inspect` covers behavior, bounds, provenance, non-execution, recovery, all tracked source files and function extraction round trips. The sanitizer gate and inspection fuzzer include symbol-limit cases. Review and Hibr's release gate precede freezing the interface.
