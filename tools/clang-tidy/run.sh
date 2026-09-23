#!/bin/bash
# usage: tools/clang-tidy/run.sh [--raw]
#
# Default: the pass/fail gate, .clang-tidy as written.
# --raw:   the backlog, not a gate. Every check group .clang-tidy turns on, with
#          none of its exclusions and no test narrowing, tallied per check and
#          split into src/+tools/ and *_test.c. Always exits 0 once it has run.
#
# The LLVM Static Analyzer (clang-analyzer-*, run through clang-tidy) plus a
# popular bugprone/cert/performance/portability ruleset for C -- see
# docs/static-analysis.md. Scope matches the sanitizers (docs/sanitizers.md):
# src/, plugins/ and tools/ in full, third_party/ excluded (vendored, upstream's
# to fix).
#
# No compile_commands.json and no Python (this repo runs neither): `bazel
# aquery` gives the exact per-file compiler invocation Bazel built each source
# with, and this script keeps only the flags clang-tidy's parser needs
# (-iquote/-isystem/-D/-std). The rest (-Wall, -frandom-seed, -MD/-MF, ...) is
# GCC-toolchain plumbing that means nothing to clang and can trip its driver on
# an unrelated GCC-only flag. A file built more than once (rare) is checked
# once, against its first compile action.
set -euo pipefail
here=$(cd "$(dirname "$0")/../.." && pwd)
cd "$here"

raw=0
case "${1-}" in
--raw) raw=1 ;;
"") ;;
*)
	echo "usage: tools/clang-tidy/run.sh [--raw]" >&2
	exit 2
	;;
esac

: "${CLANG_TIDY:=clang-tidy}"
command -v "$CLANG_TIDY" >/dev/null 2>&1 || {
	echo "run.sh: $CLANG_TIDY not found" >&2
	exit 1
}

# Materialize every generated header (LuaJIT's buildvm output, the embedded Lua
# bytecode) that the compile actions below expect to find under bazel-out.
bazel build //src/... //tools/... >&2

query='mnemonic("CppCompile", //src/... + //tools/...)'
aquery_json=$(bazel aquery --output=jsonproto "$query" 2>/dev/null)

mapfile -t files < <(jq -r '
  .actions[].arguments as $a
  | ($a | index("-c")) as $i
  | select($i != null)
  | $a[$i + 1]
' <<<"$aquery_json" | sort -u)

flags_for_file() {
	jq -r --arg f "$1" '
    first(
      .actions[]
      | select(.arguments as $a | ($a | index("-c")) as $i | $i != null and $a[$i + 1] == $f)
    ) as $action
    | $action.arguments as $a
    | [range(0; ($a | length)) as $i
        | if ($a[$i] == "-iquote" or $a[$i] == "-isystem") then [$a[$i], $a[$i + 1]]
          elif ($a[$i] | test("^-D")) then [$a[$i]]
          elif ($a[$i] | test("^-std=")) then [$a[$i]]
          else empty end
      ]
    | flatten | .[]
  ' <<<"$aquery_json"
}

# greatest.h's ASSERT/FAIL macros return out of a test on the first failure,
# skipping whatever cleanup follows -- deliberately: the harness moves on to
# the next test rather than run more code in a state a failed assertion just
# proved wrong. The analyzer's cross-function leak/null/resource checkers read
# every one of those early returns as a defect, which would flag most of the
# test suite for a pattern that is the test framework working as designed. A
# genuine bug of this shape in a test still fails under valgrind and the
# sanitizers, which run the test rather than just read it.
test_only_checks=-clang-analyzer-unix.Malloc,-clang-analyzer-unix.Stream,-clang-analyzer-core.NonNullParamChecker,-clang-analyzer-unix.StdCLibraryFunctions,-clang-analyzer-optin.portability.UnixAPI

if [ "$raw" = 1 ]; then
	# The groups .clang-tidy enables (its un-negated Checks lines), after a
	# reset: --checks appends to the config's list, so '-*' drops every
	# exclusion and the groups come back whole.
	groups=$(sed -n '/^Checks:/,/^[A-Za-z]/{/^  [a-z]/p}' .clang-tidy | tr -d ' \n')
	out=$(mktemp)
	trap 'rm -f "$out"' EXIT
	for file in "${files[@]}"; do
		mapfile -t flags < <(flags_for_file "$file")
		"$CLANG_TIDY" --quiet --checks="-*,${groups%,}" --warnings-as-errors='-*' \
			"$file" -- "${flags[@]}" 2>/dev/null >>"$out" || true
	done
	# A header finding repeats once per file that includes it: count each
	# file:line:col:check once. Paths are made repo-relative first (headers
	# come back absolute).
	grep -E '^[^ ]+:[0-9]+:[0-9]+: (warning|error): .* \[[^]]+\]$' "$out" |
		sed -E "s|^$here/||; s|^\./||" |
		sed -E 's/^([^:]+:[0-9]+:[0-9]+):.*\[([^],]+)[^]]*\]$/\1 \2/' |
		sort -u |
		awk '
		{ split($1, loc, ":"); bucket = (loc[1] ~ /_test\.c$/) ? "test" : "src"
		  n[$2, bucket]++; seen[$2] = 1; total[bucket]++ }
		END {
			printf "%-60s %8s %8s\n", "check", "src+tools", "*_test.c"
			for (c in seen) printf "%-60s %8d %8d\n", c, n[c, "src"], n[c, "test"] | "sort"
			close("sort")
			printf "%-60s %8d %8d\n", "total (" total["src"] + total["test"] ")", total["src"], total["test"]
		}'
	exit 0
fi

fail=0
for file in "${files[@]}"; do
	mapfile -t flags < <(flags_for_file "$file")
	extra_checks=()
	case "$file" in
	*_test.c) extra_checks=(--checks="$test_only_checks") ;;
	esac
	if ! "$CLANG_TIDY" --quiet "${extra_checks[@]}" "$file" -- "${flags[@]}"; then
		fail=1
	fi
done

exit "$fail"
