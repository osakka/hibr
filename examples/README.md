# examples

Complete scripts, not fragments — each one runs, and each one uses several
features together rather than demonstrating a single builtin.

| | |
|---|---|
| `ls-report.hibr` | Loads a module, uses it through the result slot, and summarises a source tree with declared arguments, maps, regex and JSON. Then unloads the module |

```
./hibr examples/ls-report.hibr --dir src --top 5
./hibr examples/ls-report.hibr --json
```

For the pieces in isolation, see [the language
documentation](../docs/language.md); for scripts that exercise edge cases
rather than read well, see [`tests/`](../tests/).
