#!/bin/sh
# usage: valgrind_case.sh <run_case.sh> <qwe-under-valgrind.sh> <qwe-binary> <suppressions> <case-dir>
#
# Runs one e2e case with run_case.sh, against a qwe that runs under valgrind
# (tools/valgrind/qwe_under_valgrind.sh). Fails if the case fails, or if any
# traced process, the step children included, left a valgrind report: a signal
# or a step's exit can hide a finding from the exit code, a log cannot.
run_case=$1 wrapper=$2 real=$3 supp=$4 case_dir=$5
real=$(cd "$(dirname "$real")" && pwd)/$(basename "$real")
supp=$(cd "$(dirname "$supp")" && pwd)/$(basename "$supp")
out=${TEST_UNDECLARED_OUTPUTS_DIR:-${TMPDIR:-/tmp}}
logs=$out/valgrind-$(basename "$case_dir")
rm -rf "$logs"
mkdir -p "$logs" || exit 3
export QWE_REAL=$real QWE_VG_LOGS=$logs QWE_VG_SUPP=$supp

sh "$run_case" "$wrapper" "$case_dir"
rc=$?
n=$(ls "$logs" | wc -l)
if [ "$n" -eq 0 ]; then
	echo "valgrind_case: no valgrind log: the case did not run under valgrind" >&2
	exit 1
fi
# --quiet leaves a log empty when there is nothing to say
bad=0
for f in "$logs"/*; do
	if [ -s "$f" ]; then
		echo "valgrind_case: findings in $(basename "$f"):" >&2
		cat "$f" >&2
		bad=1
	fi
done
[ "$rc" = 0 ] && [ "$bad" = 0 ]
