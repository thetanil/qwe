#!/bin/sh
# usage: no_threads_test.sh <qwe-debug> <threads_probe>
#
# "qwe has no threads" (docs/adr/0001-fork-per-plugin-step.md) is what lets
# concurrency-mt-unsafe (docs/static-analysis.md) stay quiet about the calls
# that are only unsafe against another thread: the exit in luavm.c's panic
# handler and the setenv of src/testing/env.h. This is what makes that true
# rather than assumed: the binary must not link the only thing that can start
# one. Every thread in glibc, including the ones behind timer_create's
# SIGEV_THREAD and aio, is started by pthread_create (thrd_create is a wrapper
# for it), so a binary that never references it never has a second thread.
qwe=$1
probe=$2
[ -f "$qwe" ] && [ -f "$probe" ] || { echo "usage: $0 <qwe-debug> <threads_probe>" >&2; exit 3; }

# Symbols of a binary, static (nm) or dynamic (nm -D: a sanitizer build).
symbols() {
	nm "$1" 2>/dev/null
	nm -D "$1" 2>/dev/null
}
creates_threads() { # <binary>
	symbols "$1" | grep -Eq '(^|[ .])(__)?(pthread_create|thrd_create)(_2_1)?(@|$)'
}

fail=0

# A check that finds nothing in any binary is no check: it must see the probe,
# and it must be reading qwe's symbols at all.
creates_threads "$probe" || { echo "FAIL: did not find pthread_create in the probe, so this check is blind" >&2; fail=1; }
symbols "$qwe" | grep -Eq ' [Tt] main$' || { echo "FAIL: could not read qwe's symbols (no main)" >&2; fail=1; }

if creates_threads "$qwe"; then
	echo "FAIL: qwe references pthread_create or thrd_create; concurrency-mt-unsafe's" >&2
	echo "  exclusions rest on qwe having no threads (see this script's header)" >&2
	fail=1
fi
exit $fail
