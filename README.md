# Shire OS

Shire OS is a small Linux-based operating environment: an existing Linux
kernel + BusyBox, assembled with Buildroot, with a custom C shell (the
**Shire Shell**) and the **CampusCompute** systems-programming application.
No kernel is written from scratch.

```
shire/
├── Makefile                 top-level wrapper (make / make test / make clean)
├── campuscompute/           the application - builds on any Linux, no Buildroot needed
│   ├── Makefile
│   ├── include/  src/  tests/
│   └── README.md            module list + Person 2 notes
├── shell/README.md          pointer: shell sources live in campuscompute/src
├── buildroot/               Buildroot external tree (packaging only, UNTESTED)
└── docs/                    DESIGN.md, MISSING_FILE_REPORT.md
```

## Build and test (Kali / Ubuntu / any Linux)
```sh
sudo apt install clang make
make            # builds campuscompute/shire and campuscompute/p2test
make test       # runs all three test suites
./campuscompute/shire          # start the shell
```
C11, `-Wall -Wextra -Wpedantic -g`, Clang by default (`make CC=gcc` also works).
Dependencies: a C compiler, `make`, glibc with POSIX threads, POSIX shared
memory and semaphores (`-pthread -lrt -lm`). Nothing else.

## Modules
| Area | Files | Requirement |
|---|---|---|
| Shire Shell | `main.c shell.c parser.c builtins.c` | FR8 |
| Job management | `jobs.c` | FR1 |
| FCFS / Round Robin / Priority | `scheduler.c` | FR2 |
| Producer-consumer logger | `logger.c` | FR3 |
| Shared-memory IPC | `ipc.c` | FR4 |
| Banker's algorithm | `banker.c` | FR5 |
| Worker process / failure detection | `worker.c` | FR6 |
| Resource limits | `limits_demo.c` | FR7 |

Details: `docs/DESIGN.md`. What was added vs. already present:
`docs/MISSING_FILE_REPORT.md`. Buildroot packaging: `buildroot/README.md`.

## Known limitations
* The shell has no pipes, redirection, globbing, variable expansion, `;`/`&`
  lists or job control (these characters are passed to programs as text).
* A child stopped with Ctrl-Z is terminated (no way to resume it).
* Buildroot packaging has not been built or booted.
* Clang's `-fsanitize` needs the `compiler-rt` package; `make sanitize CC=gcc`
  works without it.
