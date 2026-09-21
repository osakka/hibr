# hibr documentation

Start with the [project README](../README.md) for what hibr is and why it
exists. These pages are the detail.

| | |
|---|---|
| [The language](language.md) | Syntax, expansion, arithmetic, conditionals, maps and arrays, typed functions, declared arguments, errors, strict expansion |
| [Text, regex and JSON](data.md) | `str`, `arr`, `match`, `rsub`, and JSON over the map model |
| [Networking](networking.md) | Sockets, TLS, `/dev/tcp` and friends, `listen` |
| [Interactive use](interactive.md) | `~/.hibrc`, line editing, history, completion, job control |
| [The prompt](prompt.md) | The prompt hook, the segment module, and its git support |
| [Modules](modules.md) | The module ABI, and writing one |
| [Deployment](deployment.md) | `deploy.sh`: install, verify, update, roll back |
| [Testing](testing.md) | The harness, the recording discipline, sanitizers and fuzzing |
| [Decisions](adr/README.md) | Why hibr behaves the way it does, one record per decision |

Conventions for working on the code — naming, memory, what not to reintroduce —
live in [CLAUDE.md](../CLAUDE.md).
