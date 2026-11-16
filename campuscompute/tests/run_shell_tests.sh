#!/bin/sh
# End-to-end tests for the Shire Shell binary. Run from campuscompute/:
#   make test-shell        (or: sh tests/run_shell_tests.sh)
# SHIRE=<path> selects another binary (used by `make sanitize`).
# Commands are fed through stdin; output and exit status are checked.

BIN=${SHIRE:-./shire}
pass=0
fail=0
TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT

ok()  { echo "PASS  $1"; pass=$((pass + 1)); }
bad() { echo "FAIL  $1"; [ -n "$2" ] && echo "      $2"; fail=$((fail + 1)); }

# run <input>   -> sets OUT (stdout), ERR (stderr), RC (exit status)
run() {
    printf '%s\n' "$1" | "$BIN" -q >"$TMP/out" 2>"$TMP/err"
    RC=$?
    OUT=$(cat "$TMP/out")
    ERR=$(cat "$TMP/err")
}

# expect_out <name> <input> <fixed-string>   stdout must contain the text
expect_out() {
    run "$2"
    case "$OUT" in *"$3"*) ok "$1" ;; *) bad "$1" "stdout lacks '$3': $OUT" ;; esac
}
# expect_err <name> <input> <fixed-string>   stderr must contain the text
expect_err() {
    run "$2"
    case "$ERR" in *"$3"*) ok "$1" ;; *) bad "$1" "stderr lacks '$3': $ERR" ;; esac
}
# expect_rc <name> <input> <status>
expect_rc() {
    run "$2"
    if [ "$RC" -eq "$3" ]; then ok "$1"; else bad "$1" "exit status $RC, expected $3"; fi
}
# expect_no <name> <input> <fixed-string>   stdout must NOT contain the text
expect_no() {
    run "$2"
    case "$OUT" in *"$3"*) bad "$1" "stdout unexpectedly has '$3'" ;; *) ok "$1" ;; esac
}

[ -x "$BIN" ] || { echo "error: $BIN not built (run: make)"; exit 1; }

echo "--- startup, banner, options"
if "$BIN" </dev/null 2>/dev/null | grep -q "Shire Shell"; then ok "banner printed"; else bad "banner printed"; fi
if "$BIN" -q </dev/null 2>/dev/null | grep -q "Shire"; then bad "-q suppresses banner"; else ok "-q suppresses banner"; fi
"$BIN" --version | grep -q "Shire Shell 1.0.0" && ok "--version" || bad "--version"
"$BIN" --help | grep -q "Usage" && ok "--help" || bad "--help"
"$BIN" --bogus >/dev/null 2>&1; [ $? -eq 2 ] && ok "unknown option -> 2" || bad "unknown option -> 2"
"$BIN" -c "exit 5"; [ $? -eq 5 ] && ok "-c exit 5" || bad "-c exit 5"
"$BIN" -c "echo from-c" | grep -q "^from-c$" && ok "-c runs external command" || bad "-c runs external command"

echo "--- EOF / exit handling"
expect_rc "EOF on empty stdin -> 0" "" 0
run ""; [ -z "$ERR" ] && ok "EOF prints no error" || bad "EOF prints no error" "$ERR"
"$BIN" -q </dev/null; [ $? -eq 0 ] && ok "truly empty stdin -> 0" || bad "truly empty stdin -> 0"
expect_rc "exit -> 0" "exit" 0
expect_rc "exit 7" "exit 7" 7
expect_rc "exit 255" "exit 255" 255
expect_rc "exit uses last status" "false
exit" 1
expect_err "exit 256 rejected" "exit 256" "not an exit code"
expect_out "shell survives bad exit arg" "exit abc
version" "Shire Shell"
expect_err "exit with 2 args" "exit 1 2" "too many"
expect_rc "commands after exit are not run" "exit 3
echo SHOULD-NOT-RUN" 3
expect_no "commands after exit not output" "exit 3
echo SHOULD-NOT-RUN" "SHOULD-NOT-RUN"
printf 'echo no-newline' | "$BIN" -q >"$TMP/out" 2>&1
grep -q "^no-newline$" "$TMP/out" && ok "last line without newline is run" || bad "last line without newline is run"

echo "--- built-ins: help clear pwd cd version sysinfo"
expect_out "help lists schedule" "help" "schedule fcfs | rr <quantum> | priority"
for c in help clear pwd cd jobs schedule sysinfo version exit; do
    run "help"
    case "$OUT" in *"  $c "*) ok "help mentions $c" ;; *) bad "help mentions $c" ;; esac
done
expect_out "version" "version" "Shire Shell 1.0.0"
expect_err "help rejects args" "help me" "usage: help"
run "clear"; case "$OUT" in *"$(printf '\033[H\033[2J')"*) ok "clear emits ANSI sequence" ;; *) bad "clear emits ANSI sequence" ;; esac
expect_out "sysinfo kernel" "sysinfo" "Kernel"
expect_out "sysinfo memory" "sysinfo" "Memory total"
expect_out "sysinfo uptime" "sysinfo" "Uptime"
run "pwd"; [ "$OUT" = "$(pwd)" ] && ok "pwd matches cwd" || bad "pwd matches cwd" "$OUT vs $(pwd)"
expect_out "cd then pwd" "cd /tmp
pwd" "/tmp"
mkdir -p "$TMP/a b"
expect_out "cd into dir with space (quoted)" "cd \"$TMP/a b\"
pwd" "a b"
expect_out "cd - returns to previous" "cd /tmp
cd /
cd -
pwd" "/tmp"
expect_err "cd - without history" "cd -" "no previous directory"
expect_err "cd to missing dir" "cd /definitely/not/here" "No such file or directory"
expect_out "shell continues after failed cd" "cd /definitely/not/here
version" "Shire Shell"
expect_err "cd too many args" "cd a b" "too many arguments"
HOME=/tmp run "cd
pwd"; case "$OUT" in *"/tmp"*) ok "cd with no args goes to HOME" ;; *) bad "cd with no args goes to HOME" "$OUT" ;; esac
(unset HOME; printf 'cd\n' | "$BIN" -q 2>&1 >/dev/null | grep -q "HOME is not set") && ok "cd without HOME reports error" || bad "cd without HOME reports error"

echo "--- external commands"
expect_out "external echo" "echo hello world" "hello world"
expect_out "quoted arguments preserved" "echo 'a   b'" "a   b"
expect_out "no variable expansion" 'echo $HOME' '$HOME'
expect_out "pipe char is plain text" "echo a | b" "a | b"
expect_err "unknown command" "no_such_command_xyz" "command not found"
expect_rc  "unknown command -> 127" "no_such_command_xyz" 127
expect_out "shell survives unknown command" "no_such_command_xyz
version" "Shire Shell"
expect_rc "external false -> 1" "false" 1
expect_rc "external true -> 0" "true" 0
expect_rc "external exit status propagates (sh -c 'exit 9')" "sh -c 'exit 9'" 9
expect_err "child killed by signal is reported" "sh -c 'kill -TERM \$\$'" "terminated by signal 15"
expect_rc "signal death -> 128+15" "sh -c 'kill -TERM \$\$'" 143
: >"$TMP/noexec"; chmod 644 "$TMP/noexec"
expect_err "non-executable file" "$TMP/noexec" "Permission denied"
expect_rc  "non-executable -> 126" "$TMP/noexec" 126
expect_out "external child inherits cd" "cd /tmp
sh -c pwd" "/tmp"
expect_out "external output ordering preserved" "echo one
echo two
echo three" "one
two
three"
# no system(): a shell metacharacter must NOT be interpreted
rm -f "$TMP/pwned"
run "echo hi; touch $TMP/pwned"
[ ! -e "$TMP/pwned" ] && ok "';' is not interpreted (no shell injection)" || bad "';' is not interpreted"
run "echo \$(touch $TMP/pwned)"
[ ! -e "$TMP/pwned" ] && ok "\$(...) is not interpreted" || bad "\$(...) is not interpreted"

echo "--- parsing errors / input limits"
expect_err "unterminated quote" "echo \"abc" "unterminated quote"
expect_rc  "syntax error status 2" "echo 'abc" 2
expect_out "shell survives syntax error" "echo 'abc
version" "Shire Shell"
expect_err "dangling backslash" "echo abc\\" "dangling backslash"
expect_out "blank and comment lines ignored" "

   # comment
version" "Shire Shell"
LONG=$(awk 'BEGIN{for(i=0;i<1500;i++)printf "x"}')
expect_err "over-long line rejected" "$LONG" "too long"
expect_out "shell survives over-long line" "$LONG
version" "Shire Shell"
ARGS=$(awk 'BEGIN{for(i=0;i<70;i++)printf "a "}')
expect_err "too many arguments rejected" "echo $ARGS" "too many arguments"

echo "--- jobs"
expect_out "jobs on empty table" "jobs" "No jobs defined"
expect_out "jobs add" "jobs add 1 0 5 2" "Added P1"
expect_out "jobs list shows job" "jobs add 1 0 5 2
jobs list" "P1"
expect_err "duplicate id rejected" "jobs add 1 0 5 2
jobs add 1 3 3 3" "already exists"
expect_out "duplicate leaves original" "jobs add 1 0 5 2
jobs add 1 3 3 3
jobs" "       0        5"
expect_err "zero burst rejected" "jobs add 1 0 0 1" "burst"
expect_err "negative arrival rejected" "jobs add 1 -2 3 1" "arrival"
expect_err "negative priority rejected" "jobs add 1 0 3 -1" "priority"
expect_err "id 0 rejected" "jobs add 0 0 3 1" "id"
expect_err "non-numeric rejected" "jobs add 1 0 abc 1" "not an integer"
expect_err "too few args" "jobs add 1 2 3" "usage: jobs"
expect_err "too many args" "jobs add 1 2 3 4 5" "usage: jobs"
expect_err "huge number rejected" "jobs add 1 0 99999999999999999999 1" "not an integer"
expect_err "unknown subcommand" "jobs frobnicate" "usage: jobs"
expect_out "jobs clear" "jobs add 1 0 5 2
jobs clear
jobs" "No jobs defined"
expect_out "jobs reset clears results" "jobs add 1 0 5 2
schedule fcfs
jobs reset
jobs" "          5           -           -        -"
expect_out "results shown after schedule" "jobs add 1 0 5 2
schedule fcfs
jobs" "          0           5           5        0"
# 100-job limit
{ i=1; while [ $i -le 101 ]; do echo "jobs add $i 0 1 1"; i=$((i + 1)); done; } >"$TMP/many"
run "$(cat "$TMP/many")"; case "$ERR" in *"table is full"*) ok "101st job rejected" ;; *) bad "101st job rejected" "$ERR" ;; esac

echo "--- schedule"
SET="jobs add 1 0 5 0
jobs add 2 1 3 0
jobs add 3 2 8 0
jobs add 4 3 6 0"
expect_err "schedule on empty table" "schedule fcfs" "no jobs"
expect_err "schedule rr on empty table" "schedule rr 2" "no jobs"
expect_err "schedule without args" "schedule" "usage: schedule"
expect_err "schedule unknown algorithm" "schedule lottery" "usage: schedule"
expect_out "FCFS avg waiting (hand-computed 5.75)" "$SET
schedule fcfs" "Average waiting time:    5.75"
expect_out "FCFS avg turnaround (11.25)" "$SET
schedule fcfs" "Average turnaround time: 11.25"
expect_out "FCFS Gantt" "$SET
schedule fcfs" "| P1 | P2 | P3 | P4 |"
expect_out "FCFS Gantt axis" "$SET
schedule fcfs" "0    5    8    16   22"
RRSET="jobs add 1 0 5 0
jobs add 2 1 3 0
jobs add 3 2 1 0
jobs add 4 3 2 0"
expect_out "RR q=2 avg waiting (4.50)" "$RRSET
schedule rr 2" "Average waiting time:    4.50"
expect_out "RR q=2 avg turnaround (7.25)" "$RRSET
schedule rr 2" "Average turnaround time: 7.25"
expect_out "RR keeps original burst" "$RRSET
schedule rr 2
jobs" "P1              0        5        0"
expect_err "RR quantum 0" "$RRSET
schedule rr 0" "quantum"
expect_err "RR negative quantum" "$RRSET
schedule rr -2" "quantum"
expect_err "RR non-numeric quantum" "$RRSET
schedule rr two" "quantum"
expect_err "RR missing quantum" "$RRSET
schedule rr" "usage: schedule"
expect_err "RR extra args" "$RRSET
schedule rr 2 3" "usage: schedule"
PRSET="jobs add 1 0 3 2
jobs add 2 1 2 1
jobs add 3 2 1 1
jobs add 4 3 4 3"
expect_out "Priority avg waiting (2.00)" "$PRSET
schedule priority" "Average waiting time:    2.00"
expect_out "Priority avg turnaround (4.50)" "$PRSET
schedule priority" "Average turnaround time: 4.50"
expect_out "idle shown in Gantt" "jobs add 1 4 2 0
schedule fcfs" "idle"
expect_out "results differ per algorithm on same table" "$RRSET
schedule fcfs
schedule rr 2" "Round Robin"
expect_out "schedule is repeatable" "$RRSET
schedule rr 2
schedule rr 2" "Average waiting time:    4.50"

echo "--- Person 2 integration built-ins"
expect_out "banker safe" "banker safe" "SAFE state"
expect_out "banker unsafe" "banker unsafe" "UNSAFE"
expect_out "banker file" "banker file tests/banker_safe.txt" "Safe sequence"
expect_rc  "banker invalid file -> 1" "banker file tests/banker_invalid.txt" 1
expect_rc  "banker missing file -> 1" "banker file nope.txt" 1
expect_rc  "banker bad usage -> 2" "banker maybe" 2
expect_out "process normal" "process normal" "job SUCCEEDED"
expect_out "process fail" "process fail" "FAILURE DETECTED"
expect_rc  "process fail detected -> 0" "process fail" 0
expect_out "process signal" "process signal" "TERMINATED BY SIGNAL"
expect_rc  "process bad arg" "process explode" 2
expect_out "ipc" "ipc 3" "cleanup"
expect_rc  "ipc 999 rejected" "ipc 999" 2
expect_out "log" "log 2 2 3 2" "consumer"
expect_rc  "log invalid -> 1" "log 0 1 1 1" 1
expect_rc  "log wrong arg count -> 2" "log 1 2" 2
expect_out "limit fsize" "limit fsize 2000" "EFBIG"
expect_rc  "limit negative rejected" "limit fsize -5" 2
expect_out "limit does not change shell limits" "limit fsize 1000
sh -c 'ulimit -f'" "unlimited"
run "ipc 2"; ls /dev/shm 2>/dev/null | grep -q '^cc_shm_\|^sem.cc_sem_' && bad "no IPC objects left in /dev/shm" || ok "no IPC objects left in /dev/shm"
rm -f campuscompute.log

echo "--- signals"
# Ctrl-C (SIGINT) at the prompt must not kill the shell.
mkfifo "$TMP/fifo"
"$BIN" -q <"$TMP/fifo" >"$TMP/sig_out" 2>&1 &
SPID=$!
exec 3>"$TMP/fifo"
sleep 0.5
kill -INT $SPID 2>/dev/null
sleep 0.3
if kill -0 $SPID 2>/dev/null; then ok "shell survives SIGINT at prompt"; else bad "shell survives SIGINT at prompt"; fi
echo "version" >&3
echo "exit 4" >&3
exec 3>&-
wait $SPID; SRC=$?
grep -q "Shire Shell" "$TMP/sig_out" && ok "shell still executes commands after SIGINT" || bad "shell still executes commands after SIGINT"
[ "$SRC" -eq 4 ] && ok "exit status after SIGINT is correct" || bad "exit status after SIGINT" "$SRC"

# No zombie children left behind after many external commands.
run "true
true
true
true
true
ps -o stat= --ppid \$\$"
echo "$OUT" | grep -q Z && bad "no zombie children" || ok "no zombie children"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
