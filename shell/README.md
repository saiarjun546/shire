# shell/

The Shire Shell sources are **not** duplicated here. They live next to the
rest of CampusCompute so the whole application builds with a single Makefile:

| File | Role |
|---|---|
| `campuscompute/src/main.c` | entry point, options (`-q`, `-c`, `--help`, `--version`) |
| `campuscompute/src/shell.c` | banner, prompt, input loop, dispatch, `fork`/`execvp`/`waitpid` |
| `campuscompute/src/parser.c` | safe tokenizer and strict number parsing |
| `campuscompute/src/builtins.c` | `help clear pwd cd jobs schedule sysinfo version exit` + `log ipc banker process limit` |

Build with `make` in the repository root (or in `campuscompute/`); the binary
is `campuscompute/shire`. This follows the instruction already in
`campuscompute/Makefile` ("Person 1 adds ... src/jobs.c src/scheduler.c src/main.c").
