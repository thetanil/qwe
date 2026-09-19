#!/bin/sh
# Runs a command and passes only if it exits 1 (a golden mismatch).
# Any other failure (harness error, crash) still fails the test.
"$@" >/dev/null 2>&1
[ $? -eq 1 ]
