#!/bin/sh
# --run_under for --config=valgrind: runs a test binary under valgrind.
# usage: run_under.sh <test-binary> [args...]
#
# Only a compiled test is wrapped. A shell test (every e2e case, luarun_test)
# is run as it is: valgrind on a shell would trace the shell, not qwe. The e2e
# cases that are gated get their own targets (tests/e2e, valgrind_e2e_test).
here=$(cd "$(dirname "$0")" && pwd)
supp=
for d in "$0.runfiles/_main" "$RUNFILES_DIR/_main" "$here"; do
	[ -f "$d/tools/valgrind/luajit.supp" ] && supp=$d/tools/valgrind/luajit.supp && break
done
[ -n "$supp" ] || { echo "run_under: luajit.supp not found" >&2; exit 3; }

bin=$1
shift
if [ "$(head -c 4 "$bin" 2>/dev/null | od -An -c | tr -d ' ')" != '177ELF' ]; then
	exec "$bin" "$@"
fi
export QWE_OOM_PROBE_TIMEOUT=180
exec valgrind --leak-check=full --error-exitcode=1 --errors-for-leak-kinds=definite,indirect \
	--suppressions="$supp" --quiet "$bin" "$@"
