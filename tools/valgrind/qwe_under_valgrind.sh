#!/bin/sh
# Stands in for the qwe binary in an e2e case: runs the real one under valgrind.
# It execs, so a signal the harness sends reaches valgrind and the client, not a
# shell in between. Set by tests/e2e/valgrind_case.sh:
#   QWE_REAL     the real qwe binary
#   QWE_VG_LOGS  directory for one log per traced process
#   QWE_VG_SUPP  the suppression file
#
# qwe forks a child per step; that child runs Lua (child_argv) before it execs
# the step's command. Valgrind follows the fork, so the Lua is covered. It also
# follows the exec unless told not to, and tracing sh, ssh, sleep and whatever
# else a workflow runs is noise and very slow, so everything under the system
# directories is skipped. qwe itself lives under the Bazel output tree.
#
# The reports go to log files, never stderr: the case's goldens compare stderr.
# --error-exitcode=99 makes any traced process with a finding exit 99, which a
# case cannot expect (a step child that fails shows up as a failed step).
exec valgrind --quiet --leak-check=full --error-exitcode=99 \
	--errors-for-leak-kinds=definite,indirect \
	--trace-children=yes --trace-children-skip='/bin/*,/usr/*,/sbin/*,/lib/*' \
	--suppressions="$QWE_VG_SUPP" \
	--log-file="$QWE_VG_LOGS/valgrind.%p" \
	"$QWE_REAL" "$@"
