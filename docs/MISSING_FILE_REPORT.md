# Missing-file report

Repository: https://github.com/saiarjun546/shire.git, branch `main`
(commits `114d93b` Initial Commit, `9669958` Added Person 2 part).

## What was in the repository before
* `campuscompute/`: complete Person 2 implementation (`logger`, `ipc`,
  `banker`, `worker`, `limits_demo` - source + headers), `p2test.c`,
  `run_p2_tests.sh`, three Banker input files, a Makefile and a README.
* `buildroot/`, `docs/`, `shell/`: only an empty `test.txt` each.
* No top-level README, no `.gitignore`, no Person 1 code at all.

## Gaps found
| Gap | Evidence |
|---|---|
| Shire Shell missing | `campuscompute/README.md`: "Person 1 owns job management, scheduling and the shell"; `shell/` empty |
| Job management missing | same; Makefile has empty `P1_SRCS` |
| Schedulers missing | same |
| No build/test for Person 1 | Makefile only built `p2test` |
| Strict C11 build failed | `-std=c11` + Clang: `strsignal`, `clock_gettime`, `nanosleep` undeclared in `ipc.c`, `limits_demo.c`, `logger.c`, `worker.c` (Makefile used `gnu11`) |
| Compiler mismatch | Makefile/README used `gcc`; requirement says Clang |
| No Buildroot content | `buildroot/` empty |
| No top-level docs | none present |
No `TODO`/`FIXME`/placeholder code existed in the source files. No Person 2
module was empty or incomplete.

## Files created
`Makefile`, `.gitignore`, `README.md`, `docs/DESIGN.md`, `docs/MISSING_FILE_REPORT.md`,
`shell/README.md`, `buildroot/{README.md,Config.in,external.desc,external.mk}`,
`buildroot/package/campuscompute/{Config.in,campuscompute.mk}`,
`campuscompute/include/{jobs,scheduler,parser,shell,builtins}.h`,
`campuscompute/src/{jobs,scheduler,parser,shell,builtins,main}.c`,
`campuscompute/tests/{p1test.c,run_shell_tests.sh}`.

## Files modified
* `campuscompute/Makefile` - Clang default, `-std=c11 -D_POSIX_C_SOURCE=200809L`,
  `-g`, Person 1 sources, `shire`/`p1test` targets, `test`/`sanitize`/`clean`.
  Existing target names (`all`, `p2test`, `test`, `check-ipc`, `clean`) and the
  `P2_OBJS` variable are preserved.
* `campuscompute/README.md` - build instructions and Person 1 sections;
  the Person 2 sections are unchanged.

## Files intentionally left unchanged
All Person 2 sources and headers, `p2test.c`, `run_p2_tests.sh`,
`banker_*.txt`, and the three placeholder `test.txt` files (not deleted).
No public interface or function signature was changed.

## Assumptions / decisions (please confirm)
1. **Shell location:** sources are in `campuscompute/src/` (as the existing
   Makefile comment instructs), not in the top-level `shell/` directory.
2. **Priority convention:** smaller number = higher priority (documented).
3. **Name/binary:** `shire`, version `1.0.0`.
4. **Person 2 commands in the shell:** `log ipc banker process limit`, taken
   from the "suggested shell command" table in `campuscompute/README.md`.
5. **Ties:** arrival ties by job id; Round Robin arrival-at-slice-end goes
   before the preempted job.
6. **Limits:** 100 jobs, values up to 1,000,000, 1024-character lines.
7. **Feature-test macro:** `_POSIX_C_SOURCE=200809L` in the Makefile, rather than
   editing Person 2's source files, to make them compile under strict C11.
8. **Buildroot:** no requirement document existed, so only a minimal
   `BR2_EXTERNAL` package was written; it is untested.

## Remains incomplete / unverified
* Buildroot image (kernel config, defconfig, login-shell choice) - not built or booted.
* Clang sanitizer runtime was not installed in the build environment; sanitizer
  runs used GCC (`make sanitize CC=gcc`). Valgrind was also used.
