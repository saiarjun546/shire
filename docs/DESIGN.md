# Shire OS / CampusCompute - Design notes

## Layout
All C code is in `campuscompute/` (`include/` headers, `src/` modules,
`tests/`). One Makefile builds everything; it has no dependency on Buildroot.

```
main.c ──> shell.c ──> parser.c            (tokenizing, strict number parsing)
              │
              └──> builtins.c ──> jobs.c ──> scheduler.c      (FR1, FR2)
                        │
                        └──> logger.c ipc.c banker.c worker.c limits_demo.c   (FR3-FR7)
```
`jobs.c`, `scheduler.c` and `parser.c` are pure logic (no I/O except explicit
`FILE*` printing), which is why they are unit-tested directly in `p1test`.

## Shire Shell (FR8)
* **Loop:** banner (suppressed by `-q`), prompt `shire> ` only when stdin is a
  terminal, `fgets` into a 1024-byte buffer. Over-long lines are rejected and
  the rest of the line discarded.
* **Parsing:** whitespace-separated tokens, `'single'` and `"double"` quotes,
  backslash escapes, `#` comments, max 64 arguments. The result is an `argv`
  array passed straight to `execvp()`: **no `system()`, no string
  concatenation, no `/bin/sh`**. `;`, `|`, `$`, `*` etc. are ordinary text.
* **Dispatch:** built-in table lookup, otherwise `fork()` + `execvp()` +
  `waitpid()` (retried on `EINTR`). Exit status: normal = exit code,
  command not found = 127, not executable = 126, killed by signal = 128+N.
* **Signals:** SIGINT is handled without `SA_RESTART`, so Ctrl-C at the prompt
  abandons the line and re-prompts; children get default signal handling.
  Ctrl-Z/Ctrl-\ are ignored by the shell itself. EOF (Ctrl-D) exits with the last
  command's status.
* **Built-ins:** `help clear pwd cd jobs schedule sysinfo version exit`, plus the
  integration commands suggested in `campuscompute/README.md`:
  `log ipc banker process limit` (status 0 = the demo behaved as expected,
  including "failure detected" for the failure demos).
* **`cd`:** no argument = `$HOME`, `cd -` = previous directory; `PWD`/`OLDPWD`
  are kept up to date.
* **`exit [code]`:** code 0-255; default is the status of the previous command.

## Job management (FR1) - `jobs.h`
Fields: id, arrival, burst (never modified), priority, remaining, completion,
turnaround, waiting. Validation: id 1..1,000,000 and unique; arrival and
priority 0..1,000,000; burst 1..1,000,000; at most 100 jobs. Results hold `-1`
(`JOB_UNSET`) until a schedule has run; `jobs reset` restores them.

## Scheduling (FR2) - `scheduler.h`
`turnaround = completion - arrival`, `waiting = turnaround - burst`.

| Convention | Rule |
|---|---|
| Priority number | **smaller = more urgent** (0 is highest) |
| Arrival ties | smaller **job id** first (not typing order) |
| Priority ties | (priority, arrival, id) ascending |
| Round Robin ordering | jobs arriving at or before the instant a slice ends are queued **before** the preempted job |
| Idle CPU | recorded as an `idle` slice (time starts at 0) |
| Timeline | consecutive slices of one job are merged |

The three algorithms reset results first, so they can be run in any order on
the same job table. Overflow cannot occur: values <= 10^6 and <= 100 jobs.

## Person 2 modules (FR3-FR7)
Unchanged. Their design is in `campuscompute/README.md`.

## Testing
| Suite | Command | Content |
|---|---|---|
| Unit (`tests/p1test.c`) | `make test-p1` | jobs, parser, FCFS/RR/Priority against hand-computed schedules, idle periods, simultaneous arrivals, equal priorities, quanta 1..max, invalid input, 100-job limit, 500 randomised job sets x 3 algorithms checked against schedule invariants, overflow extremes |
| Person 2 (`tests/run_p2_tests.sh`) | `make test-p2` | 25 existing checks (logger, IPC, Banker, worker, limits, invalid input) |
| Shell end-to-end (`tests/run_shell_tests.sh`) | `make test-shell` | startup, EOF, exit codes, cd, external commands, injection resistance, quoting, long lines, jobs/schedule via the real binary, Person 2 built-ins, IPC cleanup, SIGINT |
| Sanitizers | `make sanitize CC=gcc` | unit + shell suites under ASan + UBSan + LSan |
