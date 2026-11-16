# CampusCompute – Person 2 modules (FR3–FR7)

OS mechanisms for the CampusCompute mini-project (UE25CS242A):
concurrent logger, shared-memory IPC, Banker's algorithm, worker failure
detection and resource limits. Person 1 owns job management, scheduling
and the shell (FR1, FR2, FR8).

## Prerequisites
Ubuntu / WSL Ubuntu with `gcc` and `make`:
```
sudo apt install build-essential
```

## Build, test, run
```
make            # builds ./p2test (stand-alone driver for these modules)
make test       # 25 automated checks, including invalid-input cases
./p2test help   # list commands
./p2test all    # run every demo + summary
```

| Command | Requirement |
|---|---|
| `./p2test logger [P C M CAP]` | FR3 producer-consumer logger (default 3 2 5 4) |
| `./p2test ipc [N]` | FR4 shared memory + named semaphores |
| `./p2test banker safe` / `unsafe` | FR5 built-in test cases |
| `./p2test banker file tests/banker_safe.txt` | FR5 state from a file |
| `./p2test worker normal` / `fail` / `signal` | FR6 normal + simulated failures |
| `./p2test limits fsize [BYTES]` / `limits cpu [SECS]` | FR7 setrlimit demos |
| `./p2test check` | lists any leftover program IPC objects in /dev/shm |

## Cleanup
- Threads are joined, and mutexes and condition variables are destroyed (FR3).
- Shared memory and semaphores are unlinked, and the program checks that they are gone (FR4).
  Names contain the PID (`/cc_shm_<pid>`), so the program never touches anybody else's objects.
- Every child is reaped with `waitpid()` (FR4, FR6, FR7).
- The FR7 test file in `/tmp` is deleted. Limits are set only in the child.
- `make clean` removes build output and `campuscompute.log`.
- Manual check: `ls /dev/shm | grep cc_` should print nothing.

## Design decisions
- **Logger:** a circular buffer guarded by one mutex, plus two condition variables
  (`not_full`, `not_empty`). Every wait is in a `while` loop to handle spurious
  wake-ups. To shut down, main joins the producers, sets `shutdown=1` and broadcasts.
  The consumers then drain the queue and exit, so no message is lost and no thread hangs.
- **IPC:** a one-slot buffer with two named semaphores: `empty` (initial value 1) and
  `full` (initial value 0). The writer waits on empty and posts full. The reader does the reverse.
  An end-of-stream flag tells the reader to stop.
- **Banker:** tie-breaking scans P0..Pn-1 in order and repeats passes. The input
  must pass validation first: no negative values, and Allocation must not exceed Maximum.
- **Worker:** the parent reads the status with `WIFEXITED`/`WEXITSTATUS` and
  `WIFSIGNALED`/`WTERMSIG`. Both failures are labelled as SIMULATED.
- **Limits:** `RLIMIT_FSIZE` is the default because it is deterministic. The child ignores
  `SIGXFSZ`, so `write()` fails with `EFBIG` once the file reaches the limit.
  The `RLIMIT_CPU` variant ends with `SIGXCPU`. `RLIMIT_CORE=0` stops it from writing a core file.

## Integration with Person 1's shell
Include the headers and add `$(P2_OBJS)` to the shell's link line (the Makefile
already defines it). Link with `-pthread -lrt`.

| Shell command (suggested) | Call | Returns |
|---|---|---|
| `log` | `logger_demo()` / `logger_run(p,c,m,cap,path)` | 0 ok, -1 error |
| `ipc` | `ipc_demo()` / `ipc_run(n)` | 0 ok, -1 error |
| `banker safe/unsafe` | `banker_demo_safe()` / `banker_demo_unsafe()` | 1 safe, 0 unsafe, -1 invalid |
| `banker file X` | `banker_load_file(X,&s)` then `banker_check_safety(&s)` | same |
| `process normal/fail` | `worker_demo(0/1/2)` | 0 success, 1 failure detected, -1 error |
| `limit` | `limits_demo()` / `limits_cpu_demo(s)` | 0 observed, -1 not |

None of these functions call `exit()`, so an error never kills the shell.
Note: the limits header is `limits_demo.h`, not `limits.h`, because a file named
`limits.h` would shadow the standard C header.

## Screenshots owned by Person 2
| # | Command |
|---|---|
| S10 | `./p2test logger` |
| S11 | `./p2test logger 4 3 10 2` (shows waits, joins and the summary) |
| S12 | `./p2test ipc` |
| S13 | `./p2test ipc` cleanup lines + `ls /dev/shm \| grep cc_` |
| S14 | `./p2test banker safe` |
| S15 | `./p2test banker unsafe` |
| S16 | `./p2test worker normal` |
| S17 | `./p2test worker fail` and `./p2test worker signal` |
| S18 | `./p2test limits fsize` |
| S20 | `./p2test check` + `ls /dev/shm \| grep cc_` + `ps` |
| S22 | `cat campuscompute.log \| head -20` |

Set the prompt first: `export SRN='PES2UG25CS259'; export PS1='[$SRN] \w\$ '`
