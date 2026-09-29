#!/bin/bash
# usage: tools/clang-tidy/coverage_test.sh [LIST_FILE]
#
# The clang-tidy gate lints what `bazel aquery` says was compiled, so a .c file no
# target builds, or one only a configuration the gate skips builds, is silently never
# linted. This compares the .c files on disk under src/, tools/ and plugins/
# (third_party/ is vendored and excluded) with the (file, flag set) list
# `tools/clang-tidy/run.sh --list` prints, and fails on any file in neither that
# list nor the allow-list below. It also pins the three cases ticket
# sca-round3/05 exists for, so a change to run.sh that stops covering them fails here:
#
#   - src/kernel/trace.c is linted both with and without QWE_NO_STRERRORNAME_NP;
#   - the two libFuzzer harness sources (manual-tagged targets) are linted;
#   - a coverage-configuration pair (-DQWE_GCOV) is in the list.
#
# Not a Bazel test: it needs `bazel aquery`, which cannot run inside a sandbox. CI runs
# it in static-analysis.yml; LIST_FILE (the evidence bundle's files.txt) skips the
# aquery when a run.sh --evidence-dir has just produced one.
set -euo pipefail
here=$(cd "$(dirname "$0")/../.." && pwd)
cd "$here"

# Files deliberately not linted, one "path<TAB>reason" per line. Empty: a file that
# is not built by any target is a file nobody has checked, and the way to fix that is
# to build it (or delete it), not to list it here.
allow=''

# missing_from DISK COVERED: the lines of DISK that are in neither COVERED nor the
# allow-list. Both arguments are newline-separated, sorted, unique path lists.
missing_from() {
	local allowed
	allowed=$(printf '%s' "$allow" | cut -f1)
	comm -23 <(printf '%s\n' "$1") <(printf '%s\n%s\n' "$2" "$allowed" | sed '/^$/d' | sort -u)
}

fail=0
# check DESCRIPTION COMMAND...: run COMMAND, and report whether it succeeded.
check() {
	local what=$1
	shift
	if "$@"; then
		echo "ok:   $what"
	else
		echo "FAIL: $what" >&2
		fail=1
	fi
}

is() { [ "$1" = "$2" ]; }
has_line() { printf '%s\n' "$2" | grep -qx -- "$1"; }
has_match() { printf '%s\n' "$2" | grep -q -- "$1"; }
# has_line_without PATTERN TEXT: some line of TEXT (an empty one counts) lacks PATTERN.
has_line_without() { [ "$(printf '%s\n' "$2" | grep -vc -- "$1" || true)" -gt 0 ]; }

# The comparison itself: it reports a file that is in neither list, and stays quiet on
# one that is in either.
check "missing_from reports files in neither list" \
	is "$(missing_from $'a.c\nb.c\nc.c' $'a.c')" $'b.c\nc.c'
allow=$'b.c\tan example reason'
check "missing_from lets an allow-listed file through" \
	is "$(missing_from $'a.c\nb.c\nc.c' $'a.c')" 'c.c'
allow=''

if [ $# -gt 1 ]; then
	echo "usage: tools/clang-tidy/coverage_test.sh [LIST_FILE]" >&2
	exit 2
fi
if [ $# -eq 1 ]; then
	listing=$(cat "$1")
else
	listing=$(tools/clang-tidy/run.sh --list)
fi

# The list is "file<TAB>configurations<TAB>variant" lines, then a count line.
pairs=$(printf '%s\n' "$listing" | grep -P '^[^\t]+\t[^\t]+\t' || true)
covered=$(printf '%s\n' "$pairs" | cut -f1 | sort -u)
disk=$(find src tools plugins -name '*.c' -not -path '*/third_party/*' | sort -u)

check "run.sh --list printed at least one (file, flag set) pair" test -n "$pairs"

uncovered=$(missing_from "$disk" "$covered")
if [ -n "$uncovered" ]; then
	printf 'FAIL: .c files no clang-tidy run covers:\n%s\n' "$uncovered" >&2
	fail=1
else
	echo "ok:   every .c under src/, tools/ and plugins/ ($(printf '%s\n' "$disk" | wc -l) files) is covered"
fi

count=$(printf '%s\n' "$pairs" | sed '/^$/d' | wc -l)
check "the list ends with its count ($count)" has_line "$count (file, flag set) pairs" "$listing"

trace=$(printf '%s\n' "$pairs" | grep -P '^src/kernel/trace\.c\t' | cut -f3 || true)
check "trace.c is linted with QWE_NO_STRERRORNAME_NP" has_match 'QWE_NO_STRERRORNAME_NP' "$trace"
check "trace.c is linted without QWE_NO_STRERRORNAME_NP (the production branch)" \
	has_line_without 'QWE_NO_STRERRORNAME_NP' "$trace"

for f in src/edge/yaml/chain_fuzz.c src/edge/yaml/transcode_fuzz.c; do
	check "$f (a manual-tagged fuzz target) is linted" has_line "$f" "$covered"
done

# A configuration that adds no -D shares its pairs' rows instead (the configuration
# column), so the variant is what shows that gcov.h's QWE_GCOV branch is compiled.
check "the coverage configuration's -DQWE_GCOV variant is linted (gcov.h's #ifdef branch)" \
	has_match '^-DQWE_GCOV' "$(printf '%s\n' "$pairs" | grep -P '^src/kernel/(proc|gcov_test)\.c\t' | cut -f3)"

exit "$fail"
