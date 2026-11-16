#!/bin/sh
# Automated tests for Person 2's modules. Run from the campuscompute folder:
#   make test
# Each line runs one command and checks its exit status.

BIN=./p2test
pass=0
fail=0

check() {
    expected=$1; shift
    if "$@" > /dev/null 2>&1; then got=0; else got=1; fi
    if [ "$got" -eq "$expected" ]; then
        echo "PASS  $*"
        pass=$((pass + 1))
    else
        echo "FAIL  $*  (exit $got, expected $expected)"
        fail=$((fail + 1))
    fi
}

# expected exit 0 = command produced the expected result
check 0 $BIN logger
check 0 $BIN logger 4 3 10 2
check 0 $BIN ipc
check 0 $BIN ipc 20
check 0 $BIN banker safe
check 0 $BIN banker unsafe
check 0 $BIN banker file tests/banker_safe.txt
check 0 $BIN banker file tests/banker_unsafe.txt
check 0 $BIN worker normal
check 0 $BIN worker fail
check 0 $BIN worker signal
check 0 $BIN limits fsize
check 0 $BIN limits fsize 2000
check 0 $BIN limits cpu
check 0 $BIN check

# expected exit 1 = invalid input must be rejected with an error
check 1 $BIN
check 1 $BIN nonsense
check 1 $BIN logger 0 1 1 1
check 1 $BIN logger 2 2 abc 4
check 1 $BIN ipc 999
check 1 $BIN banker maybe
check 1 $BIN banker file tests/banker_invalid.txt
check 1 $BIN banker file does_not_exist.txt
check 1 $BIN worker explode
check 1 $BIN limits fsize -5

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
